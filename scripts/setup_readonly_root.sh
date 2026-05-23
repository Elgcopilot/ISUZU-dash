#!/usr/bin/env bash
set -euo pipefail

HOOK_PATH="/etc/initramfs-tools/scripts/local-bottom/readonly-root-overlay"
MODULES_PATH="/etc/initramfs-tools/modules"
ARMBIAN_ENV="/boot/armbianEnv.txt"
HOOK_FLAG="isuzu_ro_root=1"
KERNEL_VER="$(uname -r)"
INITRD_IMG="/boot/initrd.img-${KERNEL_VER}"
UINITRD_IMG="/boot/uInitrd-${KERNEL_VER}"
TMPFS_SIZE="256M"
MODE="enable"

usage() {
  cat <<'EOF'
Usage: sudo ./scripts/setup_readonly_root.sh [options]

Options:
  --tmpfs-size <size>  tmpfs size for writable overlay upper/work (default: 256M)
  --disable            Remove the readonly-root setup
  --status             Show current setup state
  -h, --help           Show this help

This installs an initramfs hook that:
  1. Mounts the real root filesystem read-only during early boot.
  2. Places a writable overlay upper/work directory in RAM.
  3. Boots the system from the overlay mount, protecting the SD card root.

Notes:
  - /mnt/candata remains a normal writable ext4 mount from fstab.
  - Changes made under / after boot become ephemeral unless they target a
    separate writable mount like /mnt/candata.
  - Reboot is required after enable/disable.
EOF
}

log() {
  echo "[readonly-root] $*"
}

fail() {
  echo "ERROR: $*" >&2
  exit 1
}

require_root() {
  if [[ $(id -u) -ne 0 ]]; then
    fail "Run this script with sudo or as root"
  fi
}

backup_file() {
  local path="$1"
  if [[ -f "$path" ]]; then
    cp "$path" "${path}.bak-$(date +%Y%m%d-%H%M%S)"
  fi
}

print_status() {
  echo "Hook: $([[ -f "$HOOK_PATH" ]] && echo installed || echo missing)"
  echo "Overlay module entry: $(grep -qx 'overlay' "$MODULES_PATH" 2>/dev/null && echo present || echo missing)"
  echo "Kernel arg flag: $(grep -q "$HOOK_FLAG" "$ARMBIAN_ENV" 2>/dev/null && echo present || echo missing)"
  echo "Initrd: $([[ -f "$INITRD_IMG" ]] && echo present || echo missing)"
  echo "uInitrd: $([[ -f "$UINITRD_IMG" ]] && echo present || echo missing)"
}

parse_args() {
  while [[ $# -gt 0 ]]; do
    case "$1" in
      --tmpfs-size)
        TMPFS_SIZE="${2:-}"
        shift 2
        ;;
      --disable)
        MODE="disable"
        shift
        ;;
      --status)
        MODE="status"
        shift
        ;;
      -h|--help)
        usage
        exit 0
        ;;
      *)
        fail "Unknown option: $1"
        ;;
    esac
  done
}

install_hook() {
  install -d /etc/initramfs-tools/scripts/local-bottom
  cat > "$HOOK_PATH" <<EOF
#!/bin/sh
set -eu

PREREQ=""

prereqs() {
    echo "\$PREREQ"
}

case "\${1:-}" in
    prereqs)
        prereqs
        exit 0
        ;;
esac

. /scripts/functions

case " \$(cat /proc/cmdline) " in
    *" ${HOOK_FLAG} "*) ;;
    *)
        exit 0
        ;;
esac

modprobe overlay

mkdir -p /overlay/lower /overlay/rw /overlay/root
mount -o move "\${rootmnt}" /overlay/lower
mount -t tmpfs -o mode=0755,size=${TMPFS_SIZE},nosuid,nodev tmpfs /overlay/rw
mkdir -p /overlay/rw/upper /overlay/rw/work
mount -t overlay overlay -o lowerdir=/overlay/lower,upperdir=/overlay/rw/upper,workdir=/overlay/rw/work /overlay/root
mount -o move /overlay/root "\${rootmnt}"
mkdir -p /run/readonly-root
echo lower=/overlay/lower > /run/readonly-root/status
echo upper=tmpfs:${TMPFS_SIZE} >> /run/readonly-root/status
EOF
  chmod 0755 "$HOOK_PATH"
  log "Installed initramfs hook at $HOOK_PATH"
}

ensure_overlay_module() {
  touch "$MODULES_PATH"
  if ! grep -qx 'overlay' "$MODULES_PATH"; then
    printf 'overlay\n' >> "$MODULES_PATH"
    log "Added overlay module to $MODULES_PATH"
  fi
}

set_kernel_flag() {
  backup_file "$ARMBIAN_ENV"
  python3 - "$ARMBIAN_ENV" "$HOOK_FLAG" enable <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
flag = sys.argv[2]
mode = sys.argv[3]
lines = path.read_text().splitlines()
updated = False

for idx, line in enumerate(lines):
    if not line.startswith("extraargs="):
        continue
    parts = line[len("extraargs="):].split()
    if mode == "enable":
        for token in ("ro", flag):
            if token not in parts:
                parts.append(token)
    else:
        parts = [token for token in parts if token not in ("ro", flag)]
    lines[idx] = "extraargs=" + " ".join(parts)
    updated = True
    break

if not updated and mode == "enable":
    lines.append(f"extraargs=ro {flag}")

path.write_text("\n".join(lines) + "\n")
PY
  log "Ensured ro and ${HOOK_FLAG} are present in extraargs"
}

clear_kernel_flag() {
  if [[ -f "$ARMBIAN_ENV" ]] && grep -q '^extraargs=' "$ARMBIAN_ENV"; then
    backup_file "$ARMBIAN_ENV"
    python3 - "$ARMBIAN_ENV" "$HOOK_FLAG" disable <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
flag = sys.argv[2]
lines = path.read_text().splitlines()

for idx, line in enumerate(lines):
    if not line.startswith("extraargs="):
        continue
    parts = [token for token in line[len("extraargs="):].split() if token not in ("ro", flag)]
    lines[idx] = "extraargs=" + " ".join(parts)
    break

path.write_text("\n".join(lines) + "\n")
PY
    log "Removed ro and ${HOOK_FLAG} from extraargs"
  fi
}

rebuild_initramfs() {
  update-initramfs -u -k "$KERNEL_VER"
  mkimage -A arm64 -O linux -T ramdisk -C none -d "$INITRD_IMG" "$UINITRD_IMG"
  ln -sf "$(basename "$UINITRD_IMG")" /boot/uInitrd
  ln -sf "$(basename "$INITRD_IMG")" /boot/initrd.img
  log "Rebuilt initrd and uInitrd for $KERNEL_VER"
}

disable_setup() {
  rm -f "$HOOK_PATH"
  if [[ -f "$MODULES_PATH" ]]; then
    grep -vx 'overlay' "$MODULES_PATH" > "${MODULES_PATH}.tmp" || true
    mv "${MODULES_PATH}.tmp" "$MODULES_PATH"
  fi
  clear_kernel_flag
  rebuild_initramfs
  log "Readonly-root setup removed"
}

enable_setup() {
  command -v update-initramfs >/dev/null 2>&1 || fail "update-initramfs is required"
  command -v mkimage >/dev/null 2>&1 || fail "mkimage is required"
  [[ -f "$ARMBIAN_ENV" ]] || fail "$ARMBIAN_ENV not found"
  install_hook
  ensure_overlay_module
  set_kernel_flag
  rebuild_initramfs
  log "Readonly-root setup installed"
}

parse_args "$@"

case "$MODE" in
  status)
    print_status
    exit 0
    ;;
  enable)
    require_root
    enable_setup
    ;;
  disable)
    require_root
    disable_setup
    ;;
esac

echo
echo "Next step: reboot the board to activate the readonly lower root."
echo "After reboot, verify with:"
echo "  mount | grep ' on / '"
echo "  cat /run/readonly-root/status"
echo "  touch /root/test && reboot   # should disappear after reboot"