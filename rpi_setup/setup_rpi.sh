#!/bin/bash
# =============================================================================
# Raspberry Pi 5 - Isuzu MFD Project Setup Script
# OS: Raspberry Pi OS Lite (Bookworm, 64-bit)
# CAN: MCP2515 SPI module
# Display: HDMI framebuffer (/dev/fb0) at 800x480
# =============================================================================

set -e  # Exit immediately on any error

YELLOW='\033[1;33m'
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m' # No Color

info()    { echo -e "${GREEN}[INFO]${NC} $1"; }
warn()    { echo -e "${YELLOW}[WARN]${NC} $1"; }
error()   { echo -e "${RED}[ERROR]${NC} $1"; exit 1; }

# Must NOT be run as root directly (sudo will be called internally)
if [ "$EUID" -eq 0 ]; then
    error "Do NOT run this script as root. Run it as the normal 'elg' user."
fi

CURRENT_USER=$(whoami)
info "Running setup for user: $CURRENT_USER"

# =============================================================================
# STEP 1: System update
# =============================================================================
info "--- Step 1: Updating system packages ---"
sudo apt-get update
sudo apt-get upgrade -y

# =============================================================================
# STEP 2: Install build dependencies
# =============================================================================
info "--- Step 2: Installing build tools ---"
sudo apt-get install -y \
    build-essential \
    cmake \
    git \
    pkg-config \
    can-utils \
    evtest \
    libinput-tools \
    usbutils

# =============================================================================
# STEP 3: User group permissions
# =============================================================================
info "--- Step 3: Adding $CURRENT_USER to required groups ---"

# 'video' group gives access to /dev/fb0 (framebuffer)
sudo usermod -aG video "$CURRENT_USER"

# 'input' group gives access to /dev/input/event* (evdev touch/mouse)
sudo usermod -aG input "$CURRENT_USER"

# 'dialout' group for serial/CAN devices (optional but useful)
sudo usermod -aG dialout "$CURRENT_USER"

info "Groups added. They will take effect after re-login or reboot."

# =============================================================================
# STEP 4: Passwordless sudo for CAN ip commands
# The app calls: sudo ip link set can0 up/down
# =============================================================================
info "--- Step 4: Configuring passwordless sudo for CAN interface ---"

SUDOERS_FILE="/etc/sudoers.d/isuzu_can"
sudo bash -c "cat > $SUDOERS_FILE" << EOF
# Allow $CURRENT_USER to manage CAN interface without password
# Required by isuzu_mfd app (can_mgr.c)
$CURRENT_USER ALL=(ALL) NOPASSWD: /sbin/ip link set can0 down
$CURRENT_USER ALL=(ALL) NOPASSWD: /sbin/ip link set can0 up type can bitrate 500000 restart-ms 100
EOF

sudo chmod 0440 "$SUDOERS_FILE"
info "Sudoers rule created at $SUDOERS_FILE"

# =============================================================================
# STEP 5: Kernel modules - load CAN modules on boot
# =============================================================================
info "--- Step 5: Enabling CAN kernel modules ---"

sudo bash -c "cat >> /etc/modules" << EOF

# CAN bus modules for isuzu_mfd project
can
can_raw
mcp251x
EOF

info "CAN modules added to /etc/modules"

# =============================================================================
# STEP 6: /boot/firmware/config.txt - SPI + MCP2515 overlay
# =============================================================================
info "--- Step 6: Patching /boot/firmware/config.txt ---"

CONFIG=/boot/firmware/config.txt

# Backup original
sudo cp "$CONFIG" "${CONFIG}.backup_$(date +%Y%m%d_%H%M%S)"
info "Backed up config.txt"

# Check if already configured
if grep -q "mcp2515-can0" "$CONFIG"; then
    warn "MCP2515 overlay already exists in config.txt — skipping."
else
    sudo bash -c "cat >> $CONFIG" << 'EOF'

# ---- Isuzu MFD CAN Bus Setup ----
# Enable SPI bus (required for MCP2515)
dtparam=spi=on

# MCP2515 CAN controller overlay
# oscillator= : crystal frequency on your module (8000000 or 16000000)
# interrupt=   : GPIO pin connected to MCP2515 INT pin (default: GPIO25 = pin 22)
#
# COMMON MODULE OSCILLATORS:
#   Generic blue/green Chinese module → 8000000 (8MHz)
#   Waveshare CAN HAT                 → 12000000 (12MHz)
#   PiCAN2 HAT                        → 16000000 (16MHz)
#
# If CAN fails to init (dmesg shows "SPI transfer failed"), change oscillator value!
dtoverlay=mcp2515-can0,oscillator=8000000,interrupt=25
dtoverlay=spi-bcm2835-overlay
EOF
    info "MCP2515 overlay added to config.txt"
fi

# =============================================================================
# STEP 7: Set HDMI resolution to 800x480 in config.txt
# =============================================================================
info "--- Step 7: Setting HDMI resolution for 800x480 display ---"

if grep -q "hdmi_cvt=800 480" "$CONFIG"; then
    warn "HDMI resolution already set — skipping."
else
    sudo bash -c "cat >> $CONFIG" << 'EOF'

# ---- Isuzu MFD HDMI Display ----
# Force HDMI output at 800x480 60Hz
# If your display supports 1024x600 or other res, adjust accordingly
hdmi_group=2
hdmi_mode=87
hdmi_cvt=800 480 60 6 0 0 0
hdmi_drive=2
hdmi_force_hotplug=1
EOF
    info "HDMI resolution configured for 800x480"
fi

# =============================================================================
# STEP 8: Build the project
# =============================================================================
info "--- Step 8: Building isuzu_mfd ---"

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/build"

info "Project directory: $PROJECT_DIR"

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

if [ -f "$BUILD_DIR/isuzu_mfd" ]; then
    info "Build successful: $BUILD_DIR/isuzu_mfd"
else
    error "Build FAILED. Check output above."
fi

# =============================================================================
# DONE
# =============================================================================
echo ""
echo -e "${GREEN}=====================================================${NC}"
echo -e "${GREEN}  Setup complete! REBOOT required for all changes.   ${NC}"
echo -e "${GREEN}=====================================================${NC}"
echo ""
echo "  After rebooting, run the app with:"
echo "    cd $BUILD_DIR && ./isuzu_mfd"
echo ""
echo "  To verify CAN is up after boot:"
echo "    ip link show can0"
echo "    candump can0"
echo ""
echo "  To verify framebuffer:"
echo "    ls -l /dev/fb0"
echo "    cat /dev/urandom > /dev/fb0   # should show random pixels on screen"
echo ""
warn "REBOOT NOW with: sudo reboot"
