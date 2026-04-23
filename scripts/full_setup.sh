#!/bin/bash
# ============================================================
# Radxa ROCK 4D — Full Board Bootstrap (Single Script)
# ============================================================
# Run on a FRESH Armbian install (microSD boot):
#   sudo bash full_setup.sh
#
# This script does everything:
#   Phase 1 (before reboot): Device tree patch for eMMC + MCP2515 CAN
#   Phase 2 (after reboot):  eMMC setup, build, deploy, CI/CD runner
#
# The script auto-detects which phase to run.
# ============================================================
set -e

# --- Configuration ---
REPO_URL="https://github.com/Elgcopilot/ISUZU-dash.git"
BRANCH="isuzu_with_ui"
APP_DIR="/home/elg/isuzu_mfd"
EMMC_DEV="/dev/mmcblk0"
EMMC_PART="${EMMC_DEV}p1"
EMMC_MOUNT="/mnt/candata"
RUNNER_DIR="/home/elg/actions-runner"
RUNNER_VERSION="2.333.1"
USER_NAME="elg"
DTB_PATH="/boot/dtb-6.1.115-vendor-rk35xx/rockchip/rk3576-rock-4d.dtb"
MARKER_FILE="/root/.rock4d_phase1_done"

echo "========================================================"
echo "  Radxa ROCK 4D — Full Setup"
echo "  Board: RK3576, MCP2515 dual CAN, Samsung eMMC 116.5GB"
echo "========================================================"
echo ""

# Must run as root
if [ "$(id -u)" -ne 0 ]; then
    echo "ERROR: Run with sudo or as root"
    exit 1
fi

# ============================================================
# PHASE 1: Patch Device Tree & Boot Config
# ============================================================
phase1() {
    # ----------------------------------------------------------
    # 1.1: Install basic tools
    # ----------------------------------------------------------
    echo "[1/4] Installing tools..."
    apt-get update -qq
    apt-get install -y -qq device-tree-compiler python3

    # ----------------------------------------------------------
    # 1.2: Patch Device Tree for eMMC
    # ----------------------------------------------------------
    # Problem: eMMC controller (mmc@2a330000) is disabled in the
    #          default DTB. HS400-ES causes timeout errors.
    # Fix:    Enable controller, add power supplies, use HS200.
    # ----------------------------------------------------------
    echo "[2/4] Patching device tree for eMMC..."

    if [ ! -f "$DTB_PATH" ]; then
        echo "ERROR: DTB not found at $DTB_PATH"
        echo "Check your kernel version and adjust DTB_PATH"
        exit 1
    fi

    # Backup original
    cp "$DTB_PATH" "${DTB_PATH}.backup"

    # Decompile
    DTS_TMP="/tmp/rock4d.dts"
    dtc -q -I dtb -O dts "$DTB_PATH" -o "$DTS_TMP"

    # Patch eMMC node
    python3 << 'PYEOF'
import re

with open("/tmp/rock4d.dts", "r") as f:
    content = f.read()

# Find mmc@2a330000 (eMMC controller)
old_node = re.search(r'(mmc@2a330000 \{.*?^\t\};)', content, re.DOTALL | re.MULTILINE)
if old_node:
    node = old_node.group(0)
    new_node = node

    # Add pinctrl if missing
    if "pinctrl-names" not in new_node:
        new_node = new_node.replace(
            'reg = <0x00 0x2a330000 0x00 0x10000>;',
            'reg = <0x00 0x2a330000 0x00 0x10000>;\n\t\tpinctrl-names = "default";\n\t\tpinctrl-0 = <0x356 0x357 0x358>;'
        )

    # Enable controller
    new_node = new_node.replace('status = "disabled"', 'status = "okay"')

    # Downgrade HS400-ES to HS200 (HS400 causes timeout -110)
    new_node = new_node.replace('mmc-hs400-1_8v;\n\t\tmmc-hs400-enhanced-strobe;', 'mmc-hs200-1_8v;')

    # Add power supplies if missing
    if "vmmc-supply" not in new_node:
        new_node = new_node.replace(
            'full-pwr-cycle-in-suspend;',
            'full-pwr-cycle-in-suspend;\n\t\tvmmc-supply = <0x193>;\n\t\tvqmmc-supply = <0x192>;'
        )
    else:
        new_node = re.sub(r'vqmmc-supply = <0x193>', 'vqmmc-supply = <0x192>', new_node)

    content = content.replace(old_node.group(0), new_node)
    print("  eMMC node patched: status=okay, HS200, power supplies added")
else:
    print("  WARNING: mmc@2a330000 node not found — may already be patched")

# Update CAN oscillator frequency: 8MHz -> 16MHz
# MCP2515 crystal changed from 8MHz (0x7a1200) to 16MHz (0xF42400)
old_freq = re.search(r'(can_osc \{[^}]*clock-frequency = <)(0x[0-9a-fA-F]+)(>;[^}]*\})', content, re.DOTALL)
if old_freq:
    current_freq = old_freq.group(2)
    if current_freq == "0x7a1200":
        content = content.replace("clock-frequency = <0x7a1200>;", "clock-frequency = <0xf42400>;")
        print("  CAN oscillator: updated from 8MHz to 16MHz")
    elif current_freq == "0xf42400":
        print("  CAN oscillator: already 16MHz")
    else:
        print(f"  WARNING: CAN oscillator has unexpected frequency {current_freq}")
else:
    print("  WARNING: can_osc node not found in DTB")

# Verify MCP2515 CAN nodes exist (spi@2ad00000)
if "mcp2515@0" in content:
    print("  MCP2515 CAN nodes: already present in DTB (dual channel)")
else:
    print("  WARNING: MCP2515 not found in DTB — CAN may not work")

with open("/tmp/rock4d.dts", "w") as f:
    f.write(content)
PYEOF

    # Recompile DTB
    dtc -q -I dts -O dtb "$DTS_TMP" -o "$DTB_PATH"
    echo "  DTB updated: ${DTB_PATH}"

    # ----------------------------------------------------------
    # 1.3: Configure boot parameters
    # ----------------------------------------------------------
    # Use non-SPI DTB (SPI variant has pin conflicts with eMMC)
    # Safe console settings that keep framebuffer working
    # ----------------------------------------------------------
    echo "[3/4] Setting boot config..."

    # Get current root UUID
    ROOT_UUID=$(findmnt -n -o UUID /)

    cat > /boot/armbianEnv.txt << BOOTEOF
verbosity=0
bootlogo=false
console=tty1
extra_cmdline=quiet loglevel=0 vt.handoff=7 fbcon=map:0 fbcon=nodefer vt.global_cursor_default=0
overlay_prefix=rk35xx
fdtfile=rockchip/rk3576-rock-4d.dtb
rootdev=UUID=${ROOT_UUID}
rootfstype=ext4
usbstoragequirks=0x2537:0x1066:u,0x2537:0x1068:u
BOOTEOF

    echo "  Boot config:"
    echo "    fdtfile  = rk3576-rock-4d.dtb (non-SPI, eMMC compatible)"
    echo "    console  = tty1 (safe, keeps framebuffer)"
    echo "    rootdev  = UUID=${ROOT_UUID}"

    # ----------------------------------------------------------
    # 1.4: Mark phase 1 complete
    # ----------------------------------------------------------
    echo "[4/4] Phase 1 complete"
    touch "$MARKER_FILE"

    echo ""
    echo "========================================================"
    echo "  REBOOT REQUIRED"
    echo "========================================================"
    echo ""
    echo "  After reboot, eMMC will be detected as /dev/mmcblk0"
    echo "  Run this script again to continue setup:"
    echo ""
    echo "    sudo bash full_setup.sh"
    echo ""
    echo "  Rebooting in 10 seconds... (Ctrl+C to cancel)"
    sleep 10
    reboot
}

# ============================================================
# PHASE 2: eMMC + Build + Deploy + Service + CI/CD Runner
# ============================================================
phase2() {
    # ----------------------------------------------------------
    # 2.1: Install all build dependencies
    # ----------------------------------------------------------
    echo "[1/8] Installing dependencies..."
    apt-get update -qq
    apt-get install -y -qq \
        build-essential cmake pkg-config git \
        iproute2 can-utils \
        libdrm-dev libicu-dev jq curl

    # ----------------------------------------------------------
    # 2.2: Verify eMMC detected
    # ----------------------------------------------------------
    echo "[2/8] Checking eMMC..."
    if [ ! -b "$EMMC_DEV" ]; then
        echo "ERROR: eMMC not detected at $EMMC_DEV"
        echo "Check dmesg for errors: dmesg | grep -i mmc"
        exit 1
    fi

    # Partition if needed
    if [ ! -b "$EMMC_PART" ]; then
        echo "  Partitioning eMMC..."
        parted -s "$EMMC_DEV" mklabel gpt
        parted -s "$EMMC_DEV" mkpart primary ext4 1MiB 100%
        sleep 2
    fi

    # Format if not ext4
    FSTYPE=$(blkid -s TYPE -o value "$EMMC_PART" 2>/dev/null || true)
    if [ "$FSTYPE" != "ext4" ]; then
        echo "  Formatting eMMC as ext4..."
        mkfs.ext4 -F -L candata "$EMMC_PART"
    else
        echo "  eMMC already ext4"
    fi

    # Mount
    mkdir -p "$EMMC_MOUNT"
    mount "$EMMC_PART" "$EMMC_MOUNT" 2>/dev/null || true

    # Add to fstab
    if ! grep -q "$EMMC_MOUNT" /etc/fstab; then
        UUID=$(blkid -s UUID -o value "$EMMC_PART")
        echo "UUID=$UUID $EMMC_MOUNT ext4 defaults,noatime 0 2" >> /etc/fstab
        echo "  Added to fstab: UUID=$UUID"
    fi

    mkdir -p "$EMMC_MOUNT/app" "$EMMC_MOUNT/config" "$EMMC_MOUNT/data" "$EMMC_MOUNT/logs"
    chown -R "$USER_NAME:$USER_NAME" "$EMMC_MOUNT"
    echo "  eMMC ready: $(df -h "$EMMC_MOUNT" | tail -1 | awk '{print $2}') total, $(df -h "$EMMC_MOUNT" | tail -1 | awk '{print $4}') free"

    # ----------------------------------------------------------
    # 2.3: Verify CAN bus (MCP2515)
    # ----------------------------------------------------------
    echo "[3/8] Checking CAN bus..."
    if dmesg | grep -q "MCP2515 successfully initialized"; then
        echo "  MCP2515 CAN: OK (8MHz oscillator, SPI1)"
        ip link set can0 down 2>/dev/null || true
        ip link set can0 up type can bitrate 500000 restart-ms 100
        echo "  can0: UP at 500kbit/s"
    else
        echo "  WARNING: MCP2515 not detected in dmesg"
        echo "  CAN may not work — check wiring and SPI connection"
    fi

    # ----------------------------------------------------------
    # 2.4: Clone repo and configure git
    # ----------------------------------------------------------
    echo "[4/8] Setting up repository..."
    if [ -d "$APP_DIR/.git" ]; then
        echo "  Repo exists, pulling latest..."
        su - "$USER_NAME" -c "cd $APP_DIR && git pull origin $BRANCH"
    else
        su - "$USER_NAME" -c "git clone -b $BRANCH $REPO_URL $APP_DIR"
    fi
    su - "$USER_NAME" -c "cd $APP_DIR && git config user.email 'Elgcopilot@users.noreply.github.com' && git config user.name 'Elgcopilot'"

    # ----------------------------------------------------------
    # 2.5: Fix color depth & build
    # ----------------------------------------------------------
    echo "[5/8] Building application..."

    # Ensure LV_COLOR_DEPTH=32 (framebuffer is ARGB8888 32-bit)
    LVCONF="$APP_DIR/lv_conf.h"
    if [ -f "$LVCONF" ]; then
        sed -i 's/#define LV_COLOR_DEPTH.*16/#define LV_COLOR_DEPTH     32/' "$LVCONF"
    fi

    su - "$USER_NAME" -c "cd $APP_DIR && mkdir -p build && cd build && cmake .. -DCMAKE_BUILD_TYPE=Release && cmake --build . -j\$(nproc)"

    # Deploy binary to eMMC
    cp "$APP_DIR/build/isuzu_mfd" "$EMMC_MOUNT/app/isuzu_mfd"
    chmod +x "$EMMC_MOUNT/app/isuzu_mfd"
    echo "  Binary deployed to $EMMC_MOUNT/app/isuzu_mfd"

    # ----------------------------------------------------------
    # 2.6: Install systemd service
    # ----------------------------------------------------------
    echo "[6/8] Installing systemd service..."
    cat > /etc/systemd/system/isuzu_mfd.service << 'EOF'
[Unit]
Description=Isuzu MFD Dashboard
After=local-fs.target
RequiresMountsFor=/mnt/candata

[Service]
Type=simple
ExecStartPre=/bin/sh -c 'echo 0 > /sys/class/vtconsole/vtcon1/bind'
ExecStart=/mnt/candata/app/isuzu_mfd
WorkingDirectory=/mnt/candata/app
Restart=on-failure
RestartSec=2
StandardOutput=null
StandardError=journal

[Install]
WantedBy=multi-user.target
EOF

    systemctl daemon-reload
    systemctl enable isuzu_mfd.service
    echo "  Service installed and enabled"

    # ----------------------------------------------------------
    # 2.6b: Install boot update check service
    # ----------------------------------------------------------
    echo "  Installing boot update check service..."
    chmod +x "$APP_DIR/scripts/boot_update_check.sh"
    cp "$APP_DIR/scripts/isuzu_mfd_update.service" /etc/systemd/system/isuzu_mfd_update.service
    systemctl daemon-reload
    systemctl enable isuzu_mfd_update.service
    echo "  Boot update check enabled (auto-update on power-on)"

    # ----------------------------------------------------------
    # 2.7: Configure sudoers for CI/CD
    # ----------------------------------------------------------
    echo "[7/8] Configuring sudoers for CI/CD..."
    cat > /etc/sudoers.d/cicd-deploy << 'EOF'
elg ALL=(ALL) NOPASSWD: /bin/systemctl stop isuzu_mfd.service, /bin/systemctl start isuzu_mfd.service, /bin/systemctl restart isuzu_mfd.service, /bin/systemctl daemon-reload, /bin/systemctl is-active isuzu_mfd.service, /bin/cp
EOF
    chmod 440 /etc/sudoers.d/cicd-deploy
    visudo -c -f /etc/sudoers.d/cicd-deploy

    # ----------------------------------------------------------
    # 2.8: Install GitHub Actions Runner
    # ----------------------------------------------------------
    echo "[8/8] Installing GitHub Actions Runner..."
    if [ -f "$RUNNER_DIR/.runner" ]; then
        echo "  Runner already configured"
    else
        mkdir -p "$RUNNER_DIR"
        cd "$RUNNER_DIR"

        curl -sL "https://github.com/actions/runner/releases/download/v${RUNNER_VERSION}/actions-runner-linux-arm64-${RUNNER_VERSION}.tar.gz" -o runner.tar.gz
        tar xzf runner.tar.gz
        rm runner.tar.gz
        chown -R "$USER_NAME:$USER_NAME" "$RUNNER_DIR"

        echo ""
        echo "  ┌──────────────────────────────────────────────────────┐"
        echo "  │  Get a runner registration token:                    │"
        echo "  │                                                      │"
        echo "  │  https://github.com/Elgcopilot/ISUZU-dash/          │"
        echo "  │  settings/actions/runners/new                        │"
        echo "  │                                                      │"
        echo "  │  Select Linux > ARM64, copy the token                │"
        echo "  └──────────────────────────────────────────────────────┘"
        echo ""
        read -rp "  Paste your runner token: " RUNNER_TOKEN

        HOSTNAME=$(hostname)
        su - "$USER_NAME" -c "cd $RUNNER_DIR && ./config.sh \
            --url $REPO_URL \
            --token $RUNNER_TOKEN \
            --name ${HOSTNAME}-runner \
            --labels self-hosted,linux,arm64,rock4d \
            --work _work \
            --unattended"

        cd "$RUNNER_DIR"
        ./svc.sh install "$USER_NAME"
        ./svc.sh start
        echo "  Runner registered and running"
    fi

    # ----------------------------------------------------------
    # Optimize: CPU performance mode
    # ----------------------------------------------------------
    for cpu in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
        echo performance > "$cpu" 2>/dev/null || true
    done

    # ----------------------------------------------------------
    # Clean up phase marker
    # ----------------------------------------------------------
    rm -f "$MARKER_FILE"

    # ----------------------------------------------------------
    # Start the dashboard
    # ----------------------------------------------------------
    systemctl start isuzu_mfd.service

    echo ""
    echo "========================================================"
    echo "  SETUP COMPLETE!"
    echo "========================================================"
    echo ""
    echo "  Dashboard:  sudo systemctl status isuzu_mfd.service"
    echo "  CAN bus:    can0 @ 500kbit/s (MCP2515, SPI1)"
    echo "  Binary:     $EMMC_MOUNT/app/isuzu_mfd (eMMC)"
    echo "  CI/CD:      Push to '$BRANCH' → auto build & deploy"
    echo ""
    echo "  Verify:"
    echo "    candump can0          # Watch CAN traffic"
    echo "    cansend can0 123#DEAD # Send test frame"
    echo ""
}

# --- Run the appropriate phase ---
if [ ! -f "$MARKER_FILE" ]; then
    echo ">>> PHASE 1: Device Tree + Boot Config (requires reboot)"
    echo ""
    phase1
else
    echo ">>> PHASE 2: eMMC + Build + Deploy + CI/CD"
    echo ""
    phase2
fi
