#!/usr/bin/env bash
# Push kodi.nro to a running hbmenu (netloader) and launch it, streaming stdout.
# Usage: wsl-nxlink-run.sh [ip] [--no-stream]
set -uo pipefail

# Paths (override with environment variables if yours differ)
KODI_ROOT="${KODI_ROOT:-$HOME/kodi-switch}"      # build tree root (kodi/, build-kodi/, package/)
DEV_DIR="${DEV_DIR:-$HOME/kodi-switch}"         # where these scripts live
SRC_DIR="${SRC_DIR:-$KODI_ROOT}"                # project source root (overlay/, docs/)
source /etc/profile.d/devkit-env.sh
export PATH="$DEVKITPRO/tools/bin:$PATH"

IP="${1:-192.168.1.191}"
MODE="${2:-stream}"
NRO=${KODI_ROOT}/package/kodi.nro

if [ ! -f "$NRO" ]; then
  echo "error: $NRO missing (run wsl-package-kodi.sh)" >&2
  exit 1
fi

echo "nxlink -> $IP"
if [ "$MODE" = "stream" ]; then
  exec nxlink -a "$IP" -s "$NRO"
fi
exec nxlink -a "$IP" "$NRO"
