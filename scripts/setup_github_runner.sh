#!/bin/bash
# ============================================================
# GitHub Actions Self-Hosted Runner Setup for Radxa ROCK 4D
# Run this script ONCE on the device to register it as a runner
# ============================================================
set -e

RUNNER_DIR="/home/elg/actions-runner"
RUNNER_VERSION="2.322.0"
ARCH="arm64"

echo "=== GitHub Actions Self-Hosted Runner Setup ==="
echo ""

# Check if already installed
if [ -d "$RUNNER_DIR" ] && [ -f "$RUNNER_DIR/.runner" ]; then
    echo "Runner already installed at $RUNNER_DIR"
    echo "To reconfigure, run: cd $RUNNER_DIR && ./config.sh remove && rm -rf $RUNNER_DIR"
    exit 0
fi

# Install dependencies
echo "[1/5] Installing dependencies..."
sudo apt-get update -qq
sudo apt-get install -y -qq libicu-dev jq curl

# Create runner directory
echo "[2/5] Creating runner directory..."
mkdir -p "$RUNNER_DIR"
cd "$RUNNER_DIR"

# Download runner
echo "[3/5] Downloading GitHub Actions Runner v${RUNNER_VERSION} (${ARCH})..."
curl -sL "https://github.com/actions/runner/releases/download/v${RUNNER_VERSION}/actions-runner-linux-${ARCH}-${RUNNER_VERSION}.tar.gz" -o runner.tar.gz
tar xzf runner.tar.gz
rm runner.tar.gz

# Configure runner
echo "[4/5] Configuring runner..."
echo ""
echo "  Go to: https://github.com/Elgcopilot/ISUZU-dash/settings/actions/runners/new"
echo "  Copy the token from the 'Configure' section"
echo ""
read -rp "  Paste your runner token here: " RUNNER_TOKEN
echo ""

./config.sh \
    --url https://github.com/Elgcopilot/ISUZU-dash \
    --token "$RUNNER_TOKEN" \
    --name "rock4d-runner" \
    --labels "self-hosted,linux,arm64,rock4d" \
    --work "_work" \
    --unattended

# Install and start as system service
echo "[5/5] Installing runner as systemd service..."
sudo ./svc.sh install
sudo ./svc.sh start

echo ""
echo "=== Setup Complete ==="
echo "Runner is registered and running as a systemd service."
echo ""
echo "Useful commands:"
echo "  Status:  sudo systemctl status actions.runner.Elgcopilot-ISUZU-dash.rock4d-runner"
echo "  Logs:    sudo journalctl -u actions.runner.Elgcopilot-ISUZU-dash.rock4d-runner -f"
echo "  Stop:    cd $RUNNER_DIR && sudo ./svc.sh stop"
echo "  Start:   cd $RUNNER_DIR && sudo ./svc.sh start"
