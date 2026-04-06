#!/bin/bash
# ============================================================
# Boot Update Check — runs on startup, pulls latest code
# If there are new commits, rebuild and deploy to eMMC
# ============================================================
set -e

REPO_DIR="/home/elg/isuzu_mfd"
BRANCH="isuzu_with_ui"
EMMC_APP="/mnt/candata/app/isuzu_mfd"
LOG="/var/log/isuzu_mfd_update.log"

log() { echo "$(date '+%Y-%m-%d %H:%M:%S') $1" >> "$LOG"; }

log "=== Boot update check started ==="

# Wait for network (max 60 seconds)
for i in $(seq 1 30); do
    if ping -c1 -W2 github.com &>/dev/null; then
        break
    fi
    sleep 2
done

if ! ping -c1 -W2 github.com &>/dev/null; then
    log "No network — skipping update"
    exit 0
fi

cd "$REPO_DIR"

# Fetch latest from remote
git fetch origin "$BRANCH" 2>>"$LOG"

LOCAL=$(git rev-parse HEAD)
REMOTE=$(git rev-parse "origin/$BRANCH")

if [ "$LOCAL" = "$REMOTE" ]; then
    log "Already up to date ($LOCAL)"
    exit 0
fi

log "Update available: $LOCAL -> $REMOTE"

# Pull latest
git reset --hard "origin/$BRANCH" 2>>"$LOG"
log "Code updated to $REMOTE"

# Rebuild
log "Building..."
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release >>"$LOG" 2>&1
cmake --build . -j$(nproc) >>"$LOG" 2>&1

if [ ! -f "isuzu_mfd" ]; then
    log "ERROR: Build failed"
    exit 1
fi

# Deploy to eMMC
systemctl stop isuzu_mfd.service 2>/dev/null || true
cp isuzu_mfd "$EMMC_APP"
chmod +x "$EMMC_APP"
systemctl start isuzu_mfd.service
log "Deployed and restarted service"
log "=== Update complete ==="
