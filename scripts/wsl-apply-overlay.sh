#!/usr/bin/env bash
set -euo pipefail

cp -a "/mnt/c/Users/kidbu/OneDrive/Documents/Default Project/kodi-switch/overlay/." \
      /home/kidbu/kodi-switch/kodi/

# cp -a preserves the Windows file times. If an overlay file's timestamp is older
# than its object file, ninja decides it is up to date and never recompiles it -
# which silently shipped stale platform code more than once. Touch everything the
# overlay provides so every patched translation unit is rebuilt.
OVL="/mnt/c/Users/kidbu/OneDrive/Documents/Default Project/kodi-switch/overlay"
( cd "$OVL" && find . -type f -print0 | while IFS= read -r -d '' f; do
    touch "/home/kidbu/kodi-switch/kodi/$f"
  done )
chown -R kidbu:kidbu /home/kidbu/kodi-switch/kodi/cmake \
                     /home/kidbu/kodi-switch/kodi/xbmc/platform/switch \
                     /home/kidbu/kodi-switch/kodi/xbmc/windowing/switch

# The source tree came from a Windows checkout. The JSON-RPC generator embeds
# input lines verbatim into C strings, so CRLF here breaks the generated header.
SCHEMA=/home/kidbu/kodi-switch/kodi/xbmc/interfaces/json-rpc/schema
if [ -d "$SCHEMA" ]; then
  for f in version.txt license.txt methods.json types.json notifications.json; do
    sed -i 's/\r$//' "$SCHEMA/$f"
  done
fi

echo "Overlay applied."
