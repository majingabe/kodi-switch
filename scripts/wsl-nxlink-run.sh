#!/usr/bin/env bash
# Push kodi.nro to a running hbmenu (netloader) and launch it, streaming stdout.
# Usage: wsl-nxlink-run.sh [ip] [--no-stream]
set -uo pipefail
source /etc/profile.d/devkit-env.sh
export PATH="$DEVKITPRO/tools/bin:$PATH"

IP="${1:-192.168.1.191}"
MODE="${2:-stream}"
NRO=/home/kidbu/kodi-switch/package/kodi.nro

if [ ! -f "$NRO" ]; then
  echo "error: $NRO missing (run wsl-package-kodi.sh)" >&2
  exit 1
fi

echo "nxlink -> $IP"
if [ "$MODE" = "stream" ]; then
  exec nxlink -a "$IP" -s "$NRO"
fi
exec nxlink -a "$IP" "$NRO"
