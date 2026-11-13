#!/usr/bin/env bash
# Copy a single overlay file into the source tree and force a rebuild of just it.
# (wsl-apply-overlay.sh touches every overlay file, which recompiles them all.)
set -euo pipefail
OVL="/mnt/c/Users/kidbu/OneDrive/Documents/Default Project/kodi-switch/overlay"
SRC=/home/kidbu/kodi-switch/kodi
F="${1:?usage: wsl-sync-one.sh <relative path>}"
cp "$OVL/$F" "$SRC/$F"
touch "$SRC/$F"
echo "synced: $F"
