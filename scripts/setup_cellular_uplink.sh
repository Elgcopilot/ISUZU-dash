#!/usr/bin/env bash
set -euo pipefail

STABLE_NAME="cell0"
SERVICE_IFACE="end0"
NETPLAN_FILE="/etc/netplan/10-dhcp-all-interfaces.yaml"
LINK_FILE="/etc/systemd/network/10-quectel-cellular.link"
ROUTE_METRIC="10"
MODEM_IFACE=""
APPLY_CHANGES=0

usage() {
  cat <<'EOF'
Usage: sudo ./setup_cellular_uplink.sh [options]

Options:
  --iface <name>         Current modem interface name (auto-detect if omitted)
  --stable-name <name>   Persistent interface name to create (default: cell0)
  --service-iface <name> Local service port interface (default: end0)
  --metric <value>       Route metric for the cellular uplink (default: 10)
  --netplan <path>       Netplan file to update (default: /etc/netplan/10-dhcp-all-interfaces.yaml)
  --apply                Run netplan apply after generating config
  -h, --help             Show this help

This script:
  1. Creates a systemd .link file that renames the USB modem NIC to a stable name.
  2. Updates netplan so the current modem interface is preferred immediately.
  3. Keeps the future stable modem name configured for the next replug or reboot.
  4. Keeps the service interface local-only by disabling its DHCP routes and DNS.

The rename rule is built from the current USB path of the selected interface.
If you move the modem to another USB port later, re-run this script.
EOF
}

log() {
  echo "[cellular-setup] $*"
}

fail() {
  echo "ERROR: $*" >&2
  exit 1
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --iface)
      MODEM_IFACE="${2:-}"
      shift 2
      ;;
    --stable-name)
      STABLE_NAME="${2:-}"
      shift 2
      ;;
    --service-iface)
      SERVICE_IFACE="${2:-}"
      shift 2
      ;;
    --metric)
      ROUTE_METRIC="${2:-}"
      shift 2
      ;;
    --netplan)
      NETPLAN_FILE="${2:-}"
      shift 2
      ;;
    --apply)
      APPLY_CHANGES=1
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

if [[ $(id -u) -ne 0 ]]; then
  fail "Run this script with sudo or as root"
fi

command -v python3 >/dev/null 2>&1 || fail "python3 is required"
command -v netplan >/dev/null 2>&1 || fail "netplan is required"
command -v udevadm >/dev/null 2>&1 || fail "udevadm is required"

detect_modem_iface() {
  local iface
  local candidates=()

  while IFS= read -r iface; do
    [[ -n "$iface" ]] || continue
    [[ "$iface" == "$SERVICE_IFACE" ]] && continue
    [[ "$iface" == "$STABLE_NAME" ]] && continue
    [[ -L "/sys/class/net/$iface/device/driver" ]] || continue
    if [[ $(basename "$(readlink -f "/sys/class/net/$iface/device/driver")") == "cdc_ether" ]]; then
      candidates+=("$iface")
    fi
  done < <(ls /sys/class/net)

  if [[ ${#candidates[@]} -eq 1 ]]; then
    MODEM_IFACE="${candidates[0]}"
    return 0
  fi

  if [[ ${#candidates[@]} -eq 0 ]]; then
    fail "No cdc_ether modem interface found. Use --iface <name>."
  fi

  fail "Multiple cdc_ether interfaces found (${candidates[*]}). Use --iface <name>."
}

[[ -n "$MODEM_IFACE" ]] || detect_modem_iface
[[ -d "/sys/class/net/$MODEM_IFACE" ]] || fail "Interface $MODEM_IFACE not found"

DRIVER_PATH="/sys/class/net/$MODEM_IFACE/device/driver"
[[ -L "$DRIVER_PATH" ]] || fail "Could not resolve driver for $MODEM_IFACE"
DRIVER_NAME="$(basename "$(readlink -f "$DRIVER_PATH")")"
[[ "$DRIVER_NAME" == "cdc_ether" ]] || fail "Interface $MODEM_IFACE is using $DRIVER_NAME, expected cdc_ether"

UDEV_PATH="$(udevadm info -q property -p "/sys/class/net/$MODEM_IFACE" | awk -F= '/^ID_PATH=/{print $2; exit}')"
[[ -n "$UDEV_PATH" ]] || fail "Could not determine ID_PATH for $MODEM_IFACE"

NETPLAN_PARENT="$(dirname "$NETPLAN_FILE")"
mkdir -p "$NETPLAN_PARENT"
mkdir -p /etc/systemd/network

log "Using modem interface: $MODEM_IFACE"
log "Stable interface name: $STABLE_NAME"
log "USB path match: $UDEV_PATH"

cat > "$LINK_FILE" <<EOF
[Match]
Path=$UDEV_PATH
Driver=$DRIVER_NAME

[Link]
Name=$STABLE_NAME
EOF

log "Wrote link file: $LINK_FILE"

python3 - "$NETPLAN_FILE" "$MODEM_IFACE" "$STABLE_NAME" "$SERVICE_IFACE" "$ROUTE_METRIC" <<'PY'
from pathlib import Path
import sys

netplan_path = Path(sys.argv[1])
current_name = sys.argv[2]
stable_name = sys.argv[3]
service_iface = sys.argv[4]
metric = sys.argv[5]

modem_names = []
for name in (current_name, stable_name):
    if name and name not in modem_names:
        modem_names.append(name)

modem_blocks = []
for name in modem_names:
    modem_blocks.append(
        f'''    {name}:
      match:
        name: "{name}"
      dhcp4: yes
      dhcp6: yes
      ipv6-privacy: yes
      dhcp4-overrides:
        route-metric: {metric}
      dhcp6-overrides:
        route-metric: {metric}
'''
    )

template = f'''# Managed by setup_cellular_uplink.sh
network:
  version: 2
  renderer: networkd
  ethernets:
    {service_iface}:
      match:
        name: "{service_iface}"
      dhcp4: yes
      dhcp6: no
      dhcp4-overrides:
        use-routes: false
        use-dns: false
{''.join(modem_blocks)}    all-eth-interfaces:
      match:
        name: "eth*"
      dhcp4: yes
      dhcp6: yes
      ipv6-privacy: yes
    all-lan-interfaces:
      match:
        name: "lan[0-9]*"
      dhcp4: yes
      dhcp6: yes
      ipv6-privacy: yes
    all-wan-interfaces:
      match:
        name: "wan[0-9]*"
      dhcp4: yes
      dhcp6: yes
      ipv6-privacy: yes
'''

netplan_path.write_text(template)
PY

log "Updated netplan: $NETPLAN_FILE"

netplan generate
log "netplan generate passed"

if [[ "$APPLY_CHANGES" -eq 1 ]]; then
  netplan apply
  log "netplan apply completed"
  log "Replug or reboot later if you want the interface name to switch to $STABLE_NAME"
else
  log "Dry run complete. Re-run with --apply to activate changes"
fi

echo
echo "Next checks:"
echo "  networkctl status $MODEM_IFACE"
echo "  ip -br addr"
echo "  ip route"