#!/usr/bin/env bash
set -euo pipefail

# Radxa 4D setup script for isuzu_mfd
# - Installs dependencies
# - Builds project with CMake
# - Optionally installs/enables systemd service

CAN_IFACE="can0"
CAN_BITRATE="500000"
INSTALL_SERVICE=1
ENABLE_SERVICE=1
START_SERVICE=0
BUILD_TYPE="Release"

usage() {
  cat <<'EOF'
Usage: ./scripts/setup_radxa4d.sh [options]

Options:
  --can-iface <name>      CAN interface name (default: can0)
  --bitrate <bps>         CAN bitrate (default: 500000)
  --build-type <type>     CMake build type (default: Release)
  --no-service            Do not install systemd service
  --no-enable             Install service but do not enable it
  --start                 Start/restart service immediately
  -h, --help              Show this help

Examples:
  ./scripts/setup_radxa4d.sh
  ./scripts/setup_radxa4d.sh --can-iface can1 --bitrate 250000 --start
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --can-iface)
      CAN_IFACE="${2:-}"
      shift 2
      ;;
    --bitrate)
      CAN_BITRATE="${2:-}"
      shift 2
      ;;
    --build-type)
      BUILD_TYPE="${2:-}"
      shift 2
      ;;
    --no-service)
      INSTALL_SERVICE=0
      shift
      ;;
    --no-enable)
      ENABLE_SERVICE=0
      shift
      ;;
    --start)
      START_SERVICE=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1"
      usage
      exit 1
      ;;
  esac
done

if [[ -z "$CAN_IFACE" || -z "$CAN_BITRATE" || -z "$BUILD_TYPE" ]]; then
  echo "Invalid empty argument detected."
  usage
  exit 1
fi

if command -v sudo >/dev/null 2>&1; then
  SUDO="sudo"
else
  SUDO=""
fi

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
BIN_PATH="$BUILD_DIR/isuzu_mfd"
SERVICE_PATH="/etc/systemd/system/isuzu_mfd.service"

echo "[1/5] Installing dependencies..."
$SUDO apt-get update
$SUDO apt-get install -y \
  build-essential \
  cmake \
  pkg-config \
  git \
  iproute2 \
  can-utils \
  libdrm-dev \
  libgbm-dev \
  libinput-dev \
  libudev-dev \
  libevdev-dev \
  libxkbcommon-dev \
  libjson-c-dev

echo "[2/5] Building project ($BUILD_TYPE)..."
cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
cmake --build "$BUILD_DIR" -j"$(nproc)"

if [[ ! -x "$BIN_PATH" ]]; then
  echo "Build did not produce executable at $BIN_PATH"
  exit 1
fi

if [[ "$INSTALL_SERVICE" -eq 1 ]]; then
  echo "[3/5] Installing systemd service..."
  $SUDO install -d -m 755 /mnt/candata/app
  $SUDO install -m 755 "$BIN_PATH" /mnt/candata/app/isuzu_mfd
  TMP_SERVICE="$(mktemp)"
  cat > "$TMP_SERVICE" <<EOF
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
TimeoutStopSec=20
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
EOF

  $SUDO install -m 644 "$TMP_SERVICE" "$SERVICE_PATH"
  rm -f "$TMP_SERVICE"

  echo "[4/5] Reloading systemd..."
  $SUDO systemctl daemon-reload

  if [[ "$ENABLE_SERVICE" -eq 1 ]]; then
    $SUDO systemctl enable isuzu_mfd.service
  fi

  if [[ "$START_SERVICE" -eq 1 ]]; then
    $SUDO systemctl restart isuzu_mfd.service
  fi
else
  echo "[3/5] Service installation skipped by --no-service"
  echo "[4/5] Systemd reload skipped"
fi

echo "[5/5] Done"
echo
echo "Executable: $BIN_PATH"
if [[ "$INSTALL_SERVICE" -eq 1 ]]; then
  echo "Service: $SERVICE_PATH"
  echo "Check status: sudo systemctl status isuzu_mfd.service"
fi

echo "Manual run: sudo \"$BIN_PATH\""
