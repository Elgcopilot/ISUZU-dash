#!/bin/bash
# Quick rebuild script for isuzu_mfd
# Run this after code changes to rebuild and run

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/build"

echo "[BUILD] Building isuzu_mfd..."
cd "$BUILD_DIR"
make -j$(nproc)

echo "[RUN] Starting isuzu_mfd..."
./isuzu_mfd
