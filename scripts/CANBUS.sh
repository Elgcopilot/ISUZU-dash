#!/bin/bash
# ============================================================
# Dual MCP2515 CAN on SPI1 M1 — ROCK 4D (RK3576)
# ============================================================
# Hardware wiring (SPI1 M1 pinmux):
#
#   Signal   Pin   GPIO
#   ------   ---   ————
#   MOSI      19   GPIO1_B1
#   MISO      21   GPIO1_B2
#   SCLK      23   GPIO1_B0
#   CS0       24   GPIO1_B3   → can1 (kernel probe order)
#   CS1       26   GPIO1_B4   → can0 (kernel probe order)
#   INT0      18   GPIO2_B7   → can1 interrupt  (ATA6561 INT → Pin 18)
#   INT1      22   GPIO2_D7   → can0 interrupt  (ATA6561 INT → Pin 22)
#   VCC       17   3.3V
#   GND      20/25
#
#   Both MCP2515s use a 16 MHz crystal oscillator.
#
# This script tries two methods in order:
#   Method A — DTS overlay (preferred, Armbian overlay loader)
#     1. Writes a DTS overlay with dtc -@ (generates __local_fixups__
#        so cross-fragment phandle references like clocks=<&can_osc>
#        are resolved correctly at overlay-apply time)
#     2. Installs the .dtbo to the kernel overlay directory
#     3. Adds the overlay name to /boot/armbianEnv.txt
#
#   Method B — Direct DTB patch (fallback, no overlay loader required)
#     Used automatically when armbianEnv.txt or the overlay directory
#     is missing (e.g. vanilla kernel, non-Armbian distro).
#     1. Backs up the live kernel DTB (timestamped)
#     2. Decompiles DTB -> DTS with dtc
#     3. Python patcher (embedded below) adds:
#          - 16 MHz mcp2515-osc fixed-clock at root
#          - pinctrl entries for INT0 / INT1 pull-up config
#          - Enables SPI1 and adds mcp2515@0 / mcp2515@1 child nodes
#     4. Recompiles patched DTS -> live DTB
#
#   After either method, if can0/can1 already exist (post-reboot re-run),
#   the interfaces are configured and brought up automatically.
#
# Usage: sudo bash SPIsetup.sh [--install-only]
# ============================================================
set -e

INSTALL_ONLY=false
for arg in "$@"; do
    case "$arg" in
        --install-only)
            INSTALL_ONLY=true
            ;;
        *)
            echo "ERROR: Unknown argument '$arg'"
            echo "Usage: sudo bash SPIsetup.sh [--install-only]"
            exit 1
            ;;
    esac
done

OVERLAY_NAME="rk3576-rock-4d-dual-mcp2515"
KERNEL_VER="$(uname -r)"
OVERLAY_DIR="/boot/dtb-${KERNEL_VER}/rockchip/overlay"
DTBO_OUT="${OVERLAY_DIR}/${OVERLAY_NAME}.dtbo"
DTS_TMP="/tmp/${OVERLAY_NAME}.dts"
ARMBIAN_ENV="/boot/armbianEnv.txt"
DTB_PATH="/boot/dtb-${KERNEL_VER}/rockchip/rk3576-rock-4d.dtb"
DTS_RAW="/tmp/rk3576-rock-4d-raw.dts"
DTS_PATCHED="/tmp/rk3576-rock-4d-patched.dts"
PATCH_PY="/tmp/add_mcp2515_nodes.py"

echo "========================================================"
echo "  Dual MCP2515 SPI1 Setup — ROCK 4D (RK3576)"
echo "  Kernel : $KERNEL_VER"
echo "========================================================"

# ---- Pre-flight --------------------------------------------
if [[ "$(id -u)" -ne 0 ]]; then
    echo "ERROR: Run with sudo or as root"
    exit 1
fi

if ! command -v dtc &>/dev/null; then
    echo "Installing device-tree-compiler..."
    apt-get install -y -qq device-tree-compiler
fi

if ! command -v fdtget &>/dev/null; then
    echo "Installing u-boot-tools..."
    apt-get install -y -qq u-boot-tools
fi

if ! command -v candump &>/dev/null; then
    echo "Installing can-utils..."
    apt-get install -y -qq can-utils 2>/dev/null || true
fi

# ---- Choose method ------------------------------------------
USE_OVERLAY=false
if [[ -f "$ARMBIAN_ENV" && -d "$OVERLAY_DIR" ]]; then
    USE_OVERLAY=true
    echo "  Method : A — DTS overlay (Armbian)"
else
    echo "  Method : B — Direct DTB patch (fallback)"
    if [[ ! -f "$DTB_PATH" ]]; then
        echo "ERROR: DTB not found at $DTB_PATH"
        echo "       Neither Armbian overlay nor direct patch is possible."
        exit 1
    fi
fi
echo ""

# ============================================================
# METHOD A: DTS overlay (Armbian)
# ============================================================
sanitize_base_dtb_can() {
    if [[ ! -f "$DTB_PATH" ]]; then
        return
    fi

    local has_old_clock=false
    local has_old_can0=false
    local has_old_can1=false

    if fdtget -t u "$DTB_PATH" /can_osc clock-frequency &>/dev/null; then
        has_old_clock=true
    fi
    if fdtget "$DTB_PATH" /spi@2ad00000/mcp2515@0 compatible &>/dev/null; then
        has_old_can0=true
    fi
    if fdtget "$DTB_PATH" /spi@2ad00000/mcp2515@1 compatible &>/dev/null; then
        has_old_can1=true
    fi

    if ! $has_old_clock && ! $has_old_can0 && ! $has_old_can1; then
        echo "  Base DTB CAN nodes already clean for overlay use"
        return
    fi

    local sanitize_raw="/tmp/rk3576-rock-4d-sanitize-raw.dts"
    local sanitize_patched="/tmp/rk3576-rock-4d-sanitize-patched.dts"
    local backup="${DTB_PATH}.overlay-clean-$(date +%Y%m%d_%H%M%S)"

    echo "  Removing stale base-DTB CAN nodes before installing overlay..."
    cp "$DTB_PATH" "$backup"
    dtc -q -I dtb -O dts -o "$sanitize_raw" "$DTB_PATH"

    python3 - "$sanitize_raw" "$sanitize_patched" << 'PYEOF'
import re
import sys

src_path, dst_path = sys.argv[1], sys.argv[2]

with open(src_path, 'r') as src:
    dts = src.read()

def find_matching_brace(text, start):
    depth = 0
    for index in range(start, len(text)):
        if text[index] == '{':
            depth += 1
        elif text[index] == '}':
            depth -= 1
            if depth == 0:
                return index
    raise ValueError(f"No matching brace for node at {start}")

def remove_node(text, node_name):
    pattern = re.compile(r'(?m)^[ \t]*(?:[A-Za-z0-9_]+\s*:\s*)?' + re.escape(node_name) + r'[ \t]*\{')
    removed = False
    while True:
        match = pattern.search(text)
        if not match:
            return text, removed
        start = match.start()
        brace_open = text.index('{', match.start())
        brace_close = find_matching_brace(text, brace_open)
        end = brace_close + 1
        while end < len(text) and text[end] in ' \t':
            end += 1
        if end < len(text) and text[end] == ';':
            end += 1
        if end < len(text) and text[end] == '\n':
            end += 1
        text = text[:start] + text[end:]
        removed = True

removed_any = False
for name in ('can_osc', 'mcp2515-osc', 'mcp2515@0', 'mcp2515@1'):
    dts, removed = remove_node(dts, name)
    removed_any = removed_any or removed

if removed_any:
    print('  Sanitized old CAN nodes from base DTB')
else:
    print('  No stale CAN nodes found in base DTB')

with open(dst_path, 'w') as dst:
    dst.write(dts)
PYEOF

    dtc -q -I dts -O dtb -o "$DTB_PATH" "$sanitize_patched"
    echo "  Base DTB backup: $backup"
}

method_overlay() {
    echo "[A1/4] Sanitizing base DTB..."
    sanitize_base_dtb_can

    echo "[A2/4] Writing DTS overlay..."
    cat > "$DTS_TMP" << 'DTSEOF'
/dts-v1/;
/plugin/;

/ {
    metadata {
        title = "Enable Dual MCP2515 CAN on RK3576 ROCK 4D SPI1";
        compatible = "radxa,rock-4d";
        category = "can";
        description = "Dual MCP2515 on SPI1 M1. INT0=GPIO2_B7 (Pin18), INT1=GPIO2_D7 (Pin22). 16MHz Crystal.";
    };
};

/* fragment@0 -- 16 MHz fixed-clock for both MCP2515s */
&{/} {
    can_osc: mcp2515-osc {
        compatible = "fixed-clock";
        #clock-cells = <0>;
        clock-frequency = <16000000>;
    };
};

/* fragment@1 -- GPIO interrupt pin configuration */
&pinctrl {
    mcp2515 {
        mcp2515_int0: mcp2515-int0 {
            /* GPIO2_B7 bank2 pin15 GPIO func pull-up (Pin 18) */
            rockchip,pins = <2 15 0 &pcfg_pull_up>;
        };
        mcp2515_int1: mcp2515-int1 {
            /* GPIO2_D7 bank2 pin31 GPIO func pull-up (Pin 22) */
            rockchip,pins = <2 31 0 &pcfg_pull_up>;
        };
    };
};

/* fragment@2 -- SPI1 bus + dual MCP2515 nodes */
&spi1 {
    status = "okay";
    #address-cells = <1>;
    #size-cells = <0>;

    /* can1 (kernel): CS0 (Pin 24), INT GPIO2_B7 (Pin 18) */
    mcp2515@0 {
        compatible = "microchip,mcp2515";
        reg = <0>;
        spi-max-frequency = <10000000>;
        clocks = <&can_osc>;
        interrupt-parent = <&gpio2>;
        interrupts = <15 8>;
        pinctrl-names = "default";
        pinctrl-0 = <&mcp2515_int0>;
        status = "okay";
    };

    /* can0 (kernel): CS1 (Pin 26), INT GPIO2_D7 (Pin 22) */
    mcp2515@1 {
        compatible = "microchip,mcp2515";
        reg = <1>;
        spi-max-frequency = <10000000>;
        clocks = <&can_osc>;
        interrupt-parent = <&gpio2>;
        interrupts = <31 8>;
        pinctrl-names = "default";
        pinctrl-0 = <&mcp2515_int1>;
        status = "okay";
    };
};
DTSEOF

    echo "[A3/4] Compiling overlay..."
    # -@ generates __symbols__ + __local_fixups__ so cross-fragment
    # phandle references (clocks = <&can_osc>) resolve correctly
    dtc -@ -q -I dts -O dtb -o "$DTBO_OUT" "$DTS_TMP"
    echo "  Installed: $DTBO_OUT"

    echo "[A4/4] Updating armbianEnv.txt..."
    if ! grep -q "^overlay_prefix=" "$ARMBIAN_ENV"; then
        echo "overlay_prefix=rk35xx" >> "$ARMBIAN_ENV"
        echo "  Added overlay_prefix=rk35xx"
    fi
    if grep -q "^overlays=" "$ARMBIAN_ENV"; then
        if grep -q "$OVERLAY_NAME" "$ARMBIAN_ENV"; then
            echo "  '$OVERLAY_NAME' already listed in overlays"
        else
            sed -i "s/^overlays=.*/& ${OVERLAY_NAME}/" "$ARMBIAN_ENV"
            echo "  Appended '$OVERLAY_NAME' to overlays"
        fi
    else
        echo "overlays=${OVERLAY_NAME}" >> "$ARMBIAN_ENV"
        echo "  Created: overlays=${OVERLAY_NAME}"
    fi
    echo ""
    echo "  Current boot config:"
    grep -E "^overlay|^fdtfile" "$ARMBIAN_ENV"
}

# ============================================================
# METHOD B: Direct DTB patch (Python patcher embedded inline)
# ============================================================
method_dtb_patch() {
    # ---- Write the Python patcher --------------------------
    echo "[B1/4] Writing DTB patcher..."
    cat > "$PATCH_PY" << 'PYEOF'
#!/usr/bin/env python3
"""
Embedded MCP2515 DTB patcher — patches a decompiled RK3576 DTS.

Modifications:
  1. Append 16 MHz fixed-clock (mcp2515-osc) inside root node (before closing brace)
  2. Add mcp2515-pins pinctrl entries for INT0 / INT1 pull-up config
  3. Enable SPI1 and add mcp2515@0 (CAN0) + mcp2515@1 (CAN1) child nodes

  CAN0: CS0 (Pin 24), INT GPIO2_B7 (Pin 18)   interrupts = <0x0f 0x08>
  CAN1: CS1 (Pin 26), INT GPIO2_D7 (Pin 22)   interrupts = <0x1f 0x08>
  IRQ_TYPE_LEVEL_LOW = 8

Usage: python3 <script> <input.dts> <output.dts> <spi1_node_name> <gpio2_ph_decimal>
"""
import re, sys

def find_matching_brace(text, start):
    assert text[start] == '{'
    depth = 0
    for i in range(start, len(text)):
        if text[i] == '{': depth += 1
        elif text[i] == '}':
            depth -= 1
            if depth == 0: return i
    raise ValueError(f"No matching brace for '{{' at {start}")

def find_node_braces(dts, node_name):
    pat = re.compile(r'(?m)^[ \t]*' + re.escape(node_name) + r'[ \t]*\{')
    m = pat.search(dts)
    if not m: return None
    bo = dts.index('{', m.start())
    return (bo, find_matching_brace(dts, bo))

def patch_node_body(dts, node_name, patcher):
    r = find_node_braces(dts, node_name)
    if r is None: return False, dts
    bo, bc = r
    return True, dts[:bo+1] + patcher(dts[bo+1:bc]) + dts[bc:]

def find_all_phandles(dts):
    return [int(x, 16) for x in re.findall(r'\bphandle\s*=\s*<(0x[0-9a-fA-F]+)>', dts)]

def find_pcfg_pull_up_ph(dts):
    m = re.search(r'pcfg-pull-up\s*\{\s*bias-pull-up;\s*phandle\s*=\s*<(0x[0-9a-fA-F]+)>', dts)
    if m: return int(m.group(1), 16)
    m = re.search(r'pcfg-pull-up\s*\{\s*phandle\s*=\s*<(0x[0-9a-fA-F]+)>[^}]*bias-pull-up', dts)
    if m: return int(m.group(1), 16)
    return None

def patch_add_can_osc(dts, can_osc_ph):
    if 'mcp2515-osc' in dts:
        print("  [skip] mcp2515-osc already present"); return dts
    root_m = re.search(r'(?m)^/\s*\{', dts)
    if root_m is None:
        print("ERROR: root '/ {' not found", file=sys.stderr); sys.exit(1)
    root_bo = dts.index('{', root_m.start())
    root_bc = find_matching_brace(dts, root_bo)
    # Insert before root closing brace (after all root properties/subnodes)
    node = (
        "\n"
        "        mcp2515-osc {\n"
        "                compatible = \"fixed-clock\";\n"
        "                #clock-cells = <0x00>;\n"
        "                clock-frequency = <0xf42400>;\n"
        f"                phandle = <0x{can_osc_ph:03x}>;\n"
        "        };\n"
    )
    print(f"  [1] mcp2515-osc added (phandle 0x{can_osc_ph:x}, 16 MHz)")
    return dts[:root_bc] + node + dts[root_bc:]

def patch_add_pinctrl(dts, pcfg_ph, int0_ph, int1_ph):
    if 'mcp2515-int0' in dts:
        print("  [skip] mcp2515-pins already in pinctrl"); return dts
    entry = (
        "\n"
        "                mcp2515-pins {\n"
        "\n"
        "                        mcp2515-int0 {\n"
        "                                /* GPIO2_B7 bank2 pin15 GPIO pull-up (Pin 18) */\n"
        f"                                rockchip,pins = <0x02 0x0f 0x00 0x{pcfg_ph:03x}>;\n"
        f"                                phandle = <0x{int0_ph:03x}>;\n"
        "                        };\n"
        "\n"
        "                        mcp2515-int1 {\n"
        "                                /* GPIO2_D7 bank2 pin31 GPIO pull-up (Pin 22) */\n"
        f"                                rockchip,pins = <0x02 0x1f 0x00 0x{pcfg_ph:03x}>;\n"
        f"                                phandle = <0x{int1_ph:03x}>;\n"
        "                        };\n"
        "                };\n"
    )
    ok, dts = patch_node_body(dts, 'pinctrl', lambda b: b + entry)
    if ok: print(f"  [2] mcp2515-pins added (int0=0x{int0_ph:x} int1=0x{int1_ph:x})")
    else:  print("  [2] WARNING: pinctrl not found — skipping pinctrl entries")
    return dts

def patch_spi1(dts, spi1_name, gpio2_ph, can_osc_ph, int0_ph, int1_ph, has_pinctrl):
    if 'microchip,mcp2515' in dts:
        print("  [skip] MCP2515 nodes already in SPI1"); return dts
    p0 = (f"                        pinctrl-names = \"default\";\n"
          f"                        pinctrl-0 = <0x{int0_ph:03x}>;\n") if has_pinctrl else ""
    p1 = (f"                        pinctrl-names = \"default\";\n"
          f"                        pinctrl-0 = <0x{int1_ph:03x}>;\n") if has_pinctrl else ""
    mcp = (
        "\n"
        "                /* CAN1 (kernel): CS0 (Pin 24), INT GPIO2_B7 (Pin 18) */\n"
        "                mcp2515@0 {\n"
        "                        compatible = \"microchip,mcp2515\";\n"
        "                        reg = <0x00>;\n"
        "                        spi-max-frequency = <0x989680>;\n"
        f"                        clocks = <0x{can_osc_ph:03x}>;\n"
        f"                        interrupt-parent = <0x{gpio2_ph:03x}>;\n"
        "                        interrupts = <0x0f 0x08>;\n"
        f"{p0}"
        "                        status = \"okay\";\n"
        "                };\n"
        "\n"
        "                /* CAN0 (kernel): CS1 (Pin 26), INT GPIO2_D7 (Pin 22) */\n"
        "                mcp2515@1 {\n"
        "                        compatible = \"microchip,mcp2515\";\n"
        "                        reg = <0x01>;\n"
        "                        spi-max-frequency = <0x989680>;\n"
        f"                        clocks = <0x{can_osc_ph:03x}>;\n"
        f"                        interrupt-parent = <0x{gpio2_ph:03x}>;\n"
        "                        interrupts = <0x1f 0x08>;\n"
        f"{p1}"
        "                        status = \"okay\";\n"
        "                };\n"
    )
    def patcher(body):
        if 'status = "disabled"' in body:
            body = body.replace('status = "disabled"', 'status = "okay"', 1)
            print('  [3] SPI1 status: disabled -> okay')
        elif 'status = "okay"' not in body:
            body = '                status = "okay";\n' + body
            print('  [3] SPI1: inserted status = okay')
        else:
            print('  [3] SPI1 already okay')
        return body + mcp
    ok, dts = patch_node_body(dts, spi1_name, patcher)
    if not ok:
        print(f"ERROR: SPI1 node '{spi1_name}' not found", file=sys.stderr); sys.exit(1)
    print("  [3] mcp2515@0 (CAN0) and mcp2515@1 (CAN1) added")
    return dts

def main():
    if len(sys.argv) != 5:
        print(f"Usage: {sys.argv[0]} <in.dts> <out.dts> <spi1_name> <gpio2_ph>")
        sys.exit(1)
    in_dts, out_dts, spi1, gpio2_ph = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4])
    with open(in_dts) as f: dts = f.read()
    all_phs = find_all_phandles(dts)
    if not all_phs:
        print("ERROR: No phandles found — valid decompiled DTB?", file=sys.stderr); sys.exit(1)
    max_ph     = max(all_phs)
    can_osc_ph = max_ph + 1
    int0_ph    = max_ph + 2
    int1_ph    = max_ph + 3
    print(f"  Phandle ceiling : 0x{max_ph:x}")
    print(f"  Allocating      : can_osc=0x{can_osc_ph:x}  int0=0x{int0_ph:x}  int1=0x{int1_ph:x}")
    print(f"  GPIO2 phandle   : 0x{gpio2_ph:x}")
    pcfg_ph = find_pcfg_pull_up_ph(dts)
    if pcfg_ph:
        print(f"  pcfg-pull-up    : 0x{pcfg_ph:x}")
        has_pinctrl = True
    else:
        print("  WARNING: pcfg-pull-up not found — INT pins will have no pull-up config")
        has_pinctrl = False
    dts = patch_add_can_osc(dts, can_osc_ph)
    if has_pinctrl:
        dts = patch_add_pinctrl(dts, pcfg_ph, int0_ph, int1_ph)
    dts = patch_spi1(dts, spi1, gpio2_ph, can_osc_ph, int0_ph, int1_ph, has_pinctrl)
    with open(out_dts, 'w') as f: f.write(dts)
    print(f"\n  Patched DTS written -> {out_dts}")

if __name__ == '__main__':
    main()
PYEOF

    # ---- Locate DTB nodes ----------------------------------
    echo "[B2/4] Locating DTB nodes..."
    SPI1_PATH=$(fdtget "$DTB_PATH" /aliases spi1 2>/dev/null || true)
    if [[ -z "$SPI1_PATH" ]]; then
        echo "ERROR: 'spi1' alias not found in DTB"; exit 1
    fi
    SPI1_NAME="${SPI1_PATH#/}"
    SPI1_STATUS=$(fdtget "$DTB_PATH" "$SPI1_PATH" status 2>/dev/null || echo "missing")
    echo "  SPI1 node : $SPI1_PATH  (status: $SPI1_STATUS)"

    if fdtget "$DTB_PATH" "${SPI1_PATH}/mcp2515@0" compatible &>/dev/null; then
        echo "  MCP2515 nodes already present in DTB — nothing to patch."
        return
    fi

    GPIO2_PATH=$(fdtget "$DTB_PATH" /aliases gpio2 2>/dev/null || true)
    if [[ -z "$GPIO2_PATH" ]]; then
        echo "ERROR: 'gpio2' alias not found in DTB"; exit 1
    fi
    GPIO2_PH=$(fdtget -t u "$DTB_PATH" "$GPIO2_PATH" phandle 2>/dev/null || true)
    if [[ -z "$GPIO2_PH" ]]; then
        echo "ERROR: gpio2 phandle not found at $GPIO2_PATH"; exit 1
    fi
    echo "  GPIO2     : $GPIO2_PATH  (phandle: $GPIO2_PH)"

    # ---- Backup + decompile --------------------------------
    echo "[B3/4] Backing up and decompiling DTB..."
    BACKUP="${DTB_PATH}.bak-$(date +%Y%m%d_%H%M%S)"
    cp "$DTB_PATH" "$BACKUP"
    echo "  Backup : $BACKUP"
    dtc -q -I dtb -O dts -o "$DTS_RAW" "$DTB_PATH"
    echo "  DTS    : $DTS_RAW"

    # ---- Patch + recompile ---------------------------------
    echo "[B4/4] Patching and recompiling DTB..."
    python3 "$PATCH_PY" "$DTS_RAW" "$DTS_PATCHED" "$SPI1_NAME" "$GPIO2_PH"
    dtc -q -I dts -O dtb -o "$DTB_PATH" "$DTS_PATCHED"
    echo "  Written: $DTB_PATH"
    echo ""
    echo "  Verification:"
    echo "  SPI1 status : $(fdtget -t s "$DTB_PATH" "$SPI1_PATH" status 2>/dev/null || echo NOT FOUND)"
    echo "  mcp2515@0   : $(fdtget -t s "$DTB_PATH" "${SPI1_PATH}/mcp2515@0" compatible 2>/dev/null || echo NOT FOUND)"
    echo "  mcp2515@1   : $(fdtget -t s "$DTB_PATH" "${SPI1_PATH}/mcp2515@1" compatible 2>/dev/null || echo NOT FOUND)"
    echo "  osc freq    : $(fdtget -t u "$DTB_PATH" /mcp2515-osc clock-frequency 2>/dev/null || echo NOT FOUND) Hz"
    echo ""
    echo "  To restore original DTB:"
    echo "    sudo cp $BACKUP $DTB_PATH"
}

install_modules() {
    echo "[S1/2] Installing CAN kernel module load config..."
    cat > /etc/modules-load.d/mcp251x.conf << 'MODEOF'
mcp251x
can_dev
MODEOF
}

install_runtime_services() {
    echo "[S2/2] Installing CAN runtime services..."
    install -d /usr/local/sbin
    cat > /usr/local/sbin/can-rename.sh << 'EOF'
#!/bin/bash
set -e

SPI0_PATH=$(ls -d /sys/bus/spi/devices/spi1.0/net/* 2>/dev/null | head -n1 || true)
SPI1_PATH=$(ls -d /sys/bus/spi/devices/spi1.1/net/* 2>/dev/null | head -n1 || true)
SPI0=$(basename "$SPI0_PATH" 2>/dev/null || true)
SPI1=$(basename "$SPI1_PATH" 2>/dev/null || true)

if [[ -z "$SPI0" || -z "$SPI1" ]]; then
    echo "CAN interfaces not found"
    exit 1
fi

if [[ "$SPI0" = "can0" && "$SPI1" = "can1" ]]; then
    echo "Already correct"
    exit 0
fi

echo "Renaming: spi1.0=$SPI0->can0, spi1.1=$SPI1->can1"
ip link set "$SPI0" down 2>/dev/null || true
ip link set "$SPI1" down 2>/dev/null || true

if [[ "$SPI1" = "can0" && "$SPI0" = "can1" ]]; then
    ip link set "$SPI1" name can_tmp
    ip link set "$SPI0" name can0
    ip link set can_tmp name can1
    exit 0
fi

ip link set "$SPI0" name can0
ip link set "$SPI1" name can1
EOF
    chmod 755 /usr/local/sbin/can-rename.sh

    cat > /usr/local/sbin/can-up.sh << 'EOF'
#!/bin/bash
set -e

for iface in can0 can1; do
    if ! ip link show "$iface" >/dev/null 2>&1; then
        echo "$iface not found"
        exit 1
    fi
done

for iface in can0 can1; do
    ip link set "$iface" down 2>/dev/null || true
    ip link set "$iface" type can bitrate 500000 restart-ms 100
    ip link set "$iface" up
done
EOF
    chmod 755 /usr/local/sbin/can-up.sh

    cat > /etc/systemd/system/can-rename.service << 'EOF'
[Unit]
Description=Rename CAN interfaces (spi1.0=can0, spi1.1=can1)
Before=network.target

[Service]
Type=oneshot
RemainAfterExit=yes
ExecStart=/usr/local/sbin/can-rename.sh

[Install]
WantedBy=multi-user.target
EOF

    cat > /etc/systemd/system/can-up.service << 'EOF'
[Unit]
Description=Configure CAN interfaces (can0/can1)
After=can-rename.service
Requires=can-rename.service

[Service]
Type=oneshot
RemainAfterExit=yes
ExecStart=/usr/local/sbin/can-up.sh

[Install]
WantedBy=multi-user.target
EOF

    systemctl daemon-reload
    if systemctl list-unit-files | grep -q '^can0-setup.service'; then
        systemctl disable --now can0-setup.service >/dev/null 2>&1 || true
        rm -f /etc/systemd/system/can0-setup.service
    fi
    systemctl enable can-rename.service >/dev/null
    systemctl enable can-up.service >/dev/null
}

# ============================================================
# Run selected method
# ============================================================
if $USE_OVERLAY; then
    method_overlay
else
    method_dtb_patch
fi

install_modules
install_runtime_services

# ============================================================
# Post-setup: bring up CAN interfaces if already present
# ============================================================
echo ""
echo "========================================================"
if $INSTALL_ONLY; then
    echo "  Dual CAN overlay/service installation complete."
    echo "  Reboot required before the kernel will probe mcp2515@0/mcp2515@1."
elif ip link show can0 &>/dev/null && ip link show can1 &>/dev/null; then
    echo "  CAN interfaces detected — applying rename rule and configuring now..."
    systemctl start can-rename.service || true
    systemctl start can-up.service || true
    echo ""
    echo "  Interface assignment:"
    ip -details link show can0 | grep 'parentdev\|can state' || true
    ip -details link show can1 | grep 'parentdev\|can state' || true
    echo ""
    echo "  can0 : $(ip link show can0 | grep -o 'state [A-Z]*')"
    echo "  can1 : $(ip link show can1 | grep -o 'state [A-Z]*')"
else
    echo "  REBOOT REQUIRED to load the dual MCP2515 drivers"
fi
echo "========================================================"
echo ""
echo "  After reboot, verify:"
echo "    cat /proc/interrupts | grep spi1"
echo "    ip -details link show can0 | grep 'parentdev\|can state'"
echo "    ip -details link show can1 | grep 'parentdev\|can state'"
echo ""
echo "  Bring up and test:"
echo "    sudo ip link set can0 type can bitrate 500000 restart-ms 100"
echo "    sudo ip link set can0 up"
echo "    timeout 5 candump can0 | head -10"
echo ""
echo "  Health guide:"
echo "    can state ERROR-ACTIVE  = healthy"
echo "    can state ERROR-PASSIVE = bitrate mismatch or transceiver issue (try 250000)"
echo "    IRQ count stuck at 0    = wrong GPIO bank/pin; recheck the header pinout"
echo ""