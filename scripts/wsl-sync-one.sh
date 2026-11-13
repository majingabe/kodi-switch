#!/usr/bin/env bash
# Copy a single overlay file into the source tree and force a rebuild of just it.
# (wsl-apply-overlay.sh touches every overlay file, which recompiles them all.)
set -euo pipefail

# Paths (override with environment variables if yours differ)
KODI_ROOT="${KODI_ROOT:-$HOME/kodi-switch}"      # build tree root (kodi/, build-kodi/, package/)
DEV_DIR="${DEV_DIR:-$HOME/kodi-switch}"         # where these scripts live
SRC_DIR="${SRC_DIR:-$KODI_ROOT}"                # project source root (overlay/, docs/)
OVL="${SRC_DIR}/overlay"
SRC=${KODI_ROOT}/kodi
F="${1:?usage: wsl-sync-one.sh <relative path>}"
cp "$OVL/$F" "$SRC/$F"
touch "$SRC/$F"
echo "synced: $F"
