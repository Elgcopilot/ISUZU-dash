#!/bin/bash
# ============================================================
# Bootstrap script for a NEW Radxa ROCK 4D board
# Sets up eMMC, builds app, installs service, registers runner
# 
# Usage:
#   1. Flash Armbian to microSD and boot the board
#   2. Copy this script to the board
#   3. Run: sudo bash bootstrap_new_board.sh
# ============================================================
set -e

REPO_URL="https://github.com/Elgcopilot/ISUZU-dash.git"
BRANCH="isuzu_with_ui"
APP_DIR="/home/elg/isuzu_mfd"
EMMC_DEV="/dev/mmcblk0"
EMMC_MOUNT="/mnt/candata"
RUNNER_DIR="/home/elg/actions-runner"
RUNNER_VERSION="2.333.1"
USER_NAME="elg"

echo "========================================"
echo "  Isuzu MFD - New Board Bootstrap"
echo "========================================"
echo ""

# -----------------------------------------------------------
# STEP 1: Install build dependencies
# -----------------------------------------------------------
echo "[1/7] Installing dependencies..."
apt-get update -qq
apt-get install -y -qq build-essential cmake git libdrm-dev libicu-dev jq curl

# -----------------------------------------------------------
# STEP 2: Format and mount eMMC
# -----------------------------------------------------------
echo "[2/7] Setting up eMMC..."
if mount | grep -q "$EMMC_MOUNT"; then
    echo "  eMMC already mounted at $EMMC_MOUNT"
else
    # Partition eMMC if not already done
    if ! lsblk "${EMMC_DEV}p1" &>/dev/null; then
        echo "  Partitioning eMMC..."
        parted -s "$EMMC_DEV" mklabel gpt
        parted -s "$EMMC_DEV" mkpart primary ext4 1MiB 100%
        sleep 2
    fi

    echo "  Formatting eMMC as ext4..."
    mkfs.ext4 -F "${EMMC_DEV}p1"

    echo "  Mounting eMMC..."
    mkdir -p "$EMMC_MOUNT"
    mount "${EMMC_DEV}p1" "$EMMC_MOUNT"

    # Add to fstab for auto-mount
    if ! grep -q "$EMMC_MOUNT" /etc/fstab; then
        UUID=$(blkid -s UUID -o value "${EMMC_DEV}p1")
        echo "UUID=$UUID $EMMC_MOUNT ext4 defaults,noatime 0 2" >> /etc/fstab
        echo "  Added eMMC to fstab (UUID=$UUID)"
    fi
fi

# Create app directories on eMMC
mkdir -p "$EMMC_MOUNT/app" "$EMMC_MOUNT/config" "$EMMC_MOUNT/data" "$EMMC_MOUNT/logs"
chown -R "$USER_NAME:$USER_NAME" "$EMMC_MOUNT"

# -----------------------------------------------------------
# STEP 3: Clone repo and build
# -----------------------------------------------------------
echo "[3/7] Cloning repository and building..."
if [ -d "$APP_DIR/.git" ]; then
    echo "  Repo exists, pulling latest..."
    su - "$USER_NAME" -c "cd $APP_DIR && git pull origin $BRANCH"
else
    su - "$USER_NAME" -c "git clone -b $BRANCH $REPO_URL $APP_DIR"
fi

su - "$USER_NAME" -c "cd $APP_DIR && mkdir -p build && cd build && cmake .. -DCMAKE_BUILD_TYPE=Release && cmake --build . -j\$(nproc)"

# Copy binary to eMMC
cp "$APP_DIR/build/isuzu_mfd" "$EMMC_MOUNT/app/isuzu_mfd"
echo "  Binary deployed to $EMMC_MOUNT/app/isuzu_mfd"

# -----------------------------------------------------------
# STEP 4: Install systemd service
# -----------------------------------------------------------
echo "[4/7] Installing systemd service..."
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

# -----------------------------------------------------------
# STEP 5: Configure sudoers for CI/CD
# -----------------------------------------------------------
echo "[5/7] Configuring sudoers for CI/CD..."
cat > /etc/sudoers.d/cicd-deploy << 'EOF'
elg ALL=(ALL) NOPASSWD: /bin/systemctl stop isuzu_mfd.service, /bin/systemctl start isuzu_mfd.service, /bin/systemctl restart isuzu_mfd.service, /bin/systemctl daemon-reload, /bin/systemctl is-active isuzu_mfd.service, /bin/cp
EOF
chmod 440 /etc/sudoers.d/cicd-deploy
visudo -c -f /etc/sudoers.d/cicd-deploy
echo "  Sudoers configured"

# -----------------------------------------------------------
# STEP 6: Install GitHub Actions Runner
# -----------------------------------------------------------
echo "[6/7] Installing GitHub Actions Runner..."
if [ -f "$RUNNER_DIR/.runner" ]; then
    echo "  Runner already configured, skipping"
else
    mkdir -p "$RUNNER_DIR"
    cd "$RUNNER_DIR"

    curl -sL "https://github.com/actions/runner/releases/download/v${RUNNER_VERSION}/actions-runner-linux-arm64-${RUNNER_VERSION}.tar.gz" -o runner.tar.gz
    tar xzf runner.tar.gz
    rm runner.tar.gz
    chown -R "$USER_NAME:$USER_NAME" "$RUNNER_DIR"

    echo ""
    echo "  ┌─────────────────────────────────────────────────────────┐"
    echo "  │  Go to:                                                 │"
    echo "  │  https://github.com/Elgcopilot/ISUZU-dash/settings/    │"
    echo "  │  actions/runners/new                                    │"
    echo "  │                                                         │"
    echo "  │  Select Linux > ARM64                                   │"
    echo "  │  Copy the token from the configure section              │"
    echo "  └─────────────────────────────────────────────────────────┘"
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

# -----------------------------------------------------------
# STEP 7: Configure git
# -----------------------------------------------------------
echo "[7/7] Configuring git..."
su - "$USER_NAME" -c "cd $APP_DIR && git config user.email 'Elgcopilot@users.noreply.github.com' && git config user.name 'Elgcopilot'"

# -----------------------------------------------------------
# Start the service
# -----------------------------------------------------------
echo ""
echo "========================================"
echo "  Setup Complete!"
echo "========================================"
echo ""
echo "  Service:  sudo systemctl status isuzu_mfd.service"
echo "  Runner:   sudo systemctl status actions.runner.*.service"
echo "  Binary:   $EMMC_MOUNT/app/isuzu_mfd"
echo ""
echo "  CI/CD: Push to '$BRANCH' branch → auto build & deploy"
echo ""

systemctl start isuzu_mfd.service
echo "  Dashboard is running!"
