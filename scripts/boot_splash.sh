#!/bin/bash
# Boot splash video player
# Plays loading video before dashboard starts

VIDEO_FILE_PRIMARY="/mnt/candata/Loading.mov"
VIDEO_FILE_FALLBACK="/home/elg/Loading.mov"
VIDEO_FILE=""
STAMP_FILE="/run/boot_splash_played"

# Prevent re-playing splash if service is triggered again in the same boot.
if [ -f "$STAMP_FILE" ]; then
    exit 0
fi
touch "$STAMP_FILE"

# Resolve video path: prefer /mnt/candata, fallback to /home/elg.
if [ -f "$VIDEO_FILE_PRIMARY" ]; then
  VIDEO_FILE="$VIDEO_FILE_PRIMARY"
elif [ -f "$VIDEO_FILE_FALLBACK" ]; then
  VIDEO_FILE="$VIDEO_FILE_FALLBACK"
else
  echo "Video file not found: $VIDEO_FILE_PRIMARY or $VIDEO_FILE_FALLBACK"
  exit 0
fi

# Disable text console cursor
setterm -cursor off > /dev/tty1 2>/dev/null || true
echo 0 > /sys/class/graphics/fbcon/cursor_blink 2>/dev/null || true

# Clear the screen
clear > /dev/tty1 2>/dev/null || true

# Play video using mpv with DRM output (works with framebuffer)
# --vo=drm uses direct rendering mode for framebuffer
# --really-quiet suppresses all output
# --no-audio since we just want video
# --loop=no plays once
if ! command -v mpv >/dev/null 2>&1; then
  echo "boot_splash: mpv is not installed; skipping splash video" | systemd-cat -t boot_splash -p warning || true
  exit 0
fi

mpv --really-quiet --vo=drm --no-audio --loop=no "$VIDEO_FILE" 2>/dev/null || mpv --really-quiet --vo=fbdev2 --no-audio --loop=no "$VIDEO_FILE" 2>/dev/null || echo "boot_splash: video playback failed for $VIDEO_FILE" | systemd-cat -t boot_splash -p warning || true

exit 0
