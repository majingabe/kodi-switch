#!/usr/bin/env bash
# Copy overlay/ into the Kodi source tree and force a rebuild of every patched file.
#
# Usage:  ./wsl-apply-overlay.sh
# Env:    KODI_DIR  (default: $HOME/kodi-switch/kodi)   Kodi source checkout
#         OVERLAY   (default: ../overlay)               this repo's overlay dir
set -euo pipefail

# Paths (override with environment variables if yours differ)
KODI_ROOT="${KODI_ROOT:-$HOME/kodi-switch}"      # build tree root (kodi/, build-kodi/, package/)
DEV_DIR="${DEV_DIR:-$HOME/kodi-switch}"         # where these scripts live
SRC_DIR="${SRC_DIR:-$KODI_ROOT}"                # project source root (overlay/, docs/)

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
KODI_DIR="${KODI_DIR:-$HOME/kodi-switch/kodi}"
OVERLAY="${OVERLAY:-$HERE/../overlay}"

if [ ! -d "$KODI_DIR/xbmc" ]; then
  echo "error: KODI_DIR does not look like a Kodi tree: $KODI_DIR" >&2
  exit 1
fi
if [ ! -d "$OVERLAY" ]; then
  echo "error: overlay not found: $OVERLAY" >&2
  exit 1
fi

cp -a "$OVERLAY/." "$KODI_DIR/"

# cp -a preserves the source timestamps. If an overlay file is older than the object
# file built from it, ninja decides it is up to date and never recompiles it - which
# silently shipped stale platform code more than once. Touch everything the overlay
# provides so every patched translation unit is rebuilt.
( cd "$OVERLAY" && find . -type f -print0 | while IFS= read -r -d '' f; do
    touch "$KODI_DIR/$f"
  done )

# The source tree may come from a Windows checkout. The JSON-RPC generator embeds input
# lines verbatim into C strings, so CRLF there breaks the generated header.
SCHEMA="$KODI_DIR/xbmc/interfaces/json-rpc/schema"
if [ -d "$SCHEMA" ]; then
  for f in version.txt license.txt methods.json types.json notifications.json; do
    [ -f "$SCHEMA/$f" ] && sed -i 's/\r$//' "$SCHEMA/$f"
  done
fi

echo "Overlay applied to $KODI_DIR"
