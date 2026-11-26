#!/bin/bash
set -euo pipefail

NFS_SOURCE="192.168.7.1:/home/vboxuser/ensc351/public"
NFS_MOUNT="/mnt/remote"

R5_NAME="79000000.r5f"
FIRMWARE="photobooth-r5-cancel.elf"
APP="$NFS_MOUNT/myApps/hello_world"

# Ensure script is running as root
if [ "$EUID" -ne 0 ]; then
    echo "Please run this script using sudo."
    exit 1
fi

echo "================================"
echo " Autonomous Photo Booth Startup"
echo "================================"

# Step 1: Locate the correct R5 processor
echo "[1/6] Locating MCU R5 processor..."

R5=""

for r in /sys/class/remoteproc/remoteproc*; do
    if [ "$(cat "$r/name")" = "$R5_NAME" ]; then
        R5="$r"
        break
    fi
done

if [ -z "$R5" ]; then
    echo "ERROR: MCU R5 processor not found."
    exit 1
fi

echo "Found R5 at $R5"
echo "Current state: $(cat "$R5/state")"

# Step 2: Mount NFS if necessary
echo "[2/6] Checking NFS mount..."

mkdir -p "$NFS_MOUNT"

if ! mountpoint -q "$NFS_MOUNT"; then
    mount -t nfs "$NFS_SOURCE" "$NFS_MOUNT"
else
    echo "NFS already mounted."
fi

# Step 3: Verify required files
echo "[3/6] Checking firmware and application..."

if [ ! -f "$NFS_MOUNT/$FIRMWARE" ]; then
    echo "ERROR: Firmware not found."
    exit 1
fi

if [ ! -x "$APP" ]; then
    echo "ERROR: Application not found or not executable."
    exit 1
fi

file "$NFS_MOUNT/$FIRMWARE"

# Step 4: Initialize R5
echo "[4/6] Initializing R5 firmware..."

STATE=$(cat "$R5/state")
CURRENT_FW=$(cat "$R5/firmware")

if [ "$STATE" = "running" ] &&
   [ "$CURRENT_FW" = "$FIRMWARE" ] &&
   cmp -s "$NFS_MOUNT/$FIRMWARE" "/lib/firmware/$FIRMWARE"; then

    echo "Correct R5 firmware already running."

else
    if [ "$STATE" = "running" ]; then
        echo "Stopping existing R5 firmware..."
        echo stop > "$R5/state"
    fi

    if [ "$(cat "$R5/state")" != "offline" ]; then
        echo "ERROR: R5 is not offline."
        exit 1
    fi

    echo "Copying firmware..."
    cp "$NFS_MOUNT/$FIRMWARE" /lib/firmware/

    echo "Selecting firmware..."
    echo "$FIRMWARE" > "$R5/firmware"

    echo "Starting R5..."

    if ! echo start > "$R5/state"; then
        echo "ERROR: R5 failed to start."
        dmesg | tail -30
        exit 1
    fi
fi

# Step 5: Verify R5
echo "[5/6] Verifying R5 status..."

if [ "$(cat "$R5/state")" != "running" ]; then
    echo "ERROR: R5 is not running."
    exit 1
fi

if [ "$(cat "$R5/firmware")" != "$FIRMWARE" ]; then
    echo "ERROR: Incorrect R5 firmware."
    exit 1
fi

echo "R5 state: $(cat "$R5/state")"
echo "R5 firmware: $(cat "$R5/firmware")"

# Step 6: Initialize HDMI display
echo "[6/8] Initializing HDMI display..."

HDMI="/sys/class/drm/card0-HDMI-A-1"

if [ -e "$HDMI/status" ]; then
    echo on > "$HDMI/status"
    sleep 1
    echo "HDMI status: $(cat "$HDMI/status")"
fi

# Hide framebuffer console cursor
if [ -e /sys/class/graphics/fbcon/cursor_blink ]; then
    echo 0 > /sys/class/graphics/fbcon/cursor_blink
fi

printf '\033[?25l' > /dev/tty1

# Step 7: Initialize photo storage
echo "[7/8] Initializing photo storage..."

mkdir -p /var/lib/photobooth/photos

# Step 8: Launch application
echo "[8/8] Starting photo booth application..."

echo "================================"
echo " Startup complete"
echo "================================"

cd "$NFS_MOUNT/myApps"
exec "$APP"