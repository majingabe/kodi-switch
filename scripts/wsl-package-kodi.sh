#!/usr/bin/env bash
set -euo pipefail

# Paths (override with environment variables if yours differ)
KODI_ROOT="${KODI_ROOT:-$HOME/kodi-switch}"      # build tree root (kodi/, build-kodi/, package/)
DEV_DIR="${DEV_DIR:-$HOME/kodi-switch}"         # where these scripts live
SRC_DIR="${SRC_DIR:-$KODI_ROOT}"                # project source root (overlay/, docs/)

BUILD=${KODI_ROOT}/build-kodi
SRC=${KODI_ROOT}/kodi
OUT=${KODI_ROOT}/package
TOOLS=/opt/devkitpro/tools/bin

if [ ! -x "$BUILD/kodi.bin" ]; then
  echo "error: $BUILD/kodi.bin not found" >&2
  exit 1
fi

rm -rf "$OUT"
mkdir -p "$OUT/kodi"

# Strip the ELF but keep a copy for debugging.
cp "$BUILD/kodi.bin" "$OUT/kodi.elf"
/opt/devkitpro/devkitA64/bin/aarch64-none-elf-strip \
  --strip-unneeded "$OUT/kodi.elf"

# The NRO icon must be a JPEG.
python3 - "$SRC/media/icon256x256.png" "$OUT/icon.jpg" <<'PY'
import sys
from PIL import Image
Image.open(sys.argv[1]).convert("RGB").save(sys.argv[2], "JPEG", quality=95)
PY

"$TOOLS/nacptool" --create "Kodi" "Kodi Switch homebrew port" "0.1.0" "$OUT/kodi.nacp"
"$TOOLS/elf2nro" "$OUT/kodi.elf" "$OUT/kodi.nro" \
  --icon="$OUT/icon.jpg" --nacp="$OUT/kodi.nacp"

# Runtime data root: sd:/switch/kodi
if [ -d "$BUILD/addons" ]; then
  cp -a "$BUILD/addons" "$OUT/kodi/addons"
else
  cp -a "$SRC/addons" "$OUT/kodi/addons"
fi
if [ -d "$BUILD/system" ]; then
  cp -a "$BUILD/system" "$OUT/kodi/system"
else
  cp -a "$SRC/system" "$OUT/kodi/system"
fi
cp -a "$SRC/media" "$OUT/kodi/media"

# Add-ons that live in the source tree but are not part of Kodi's CMake add-on
# build. service.switch.repair re-applies the enabled state after a database rebuild.
for extra in service.switch.repair; do
  if [ -d "$SRC/addons/$extra" ] && [ ! -d "$OUT/kodi/addons/$extra" ]; then
    cp -a "$SRC/addons/$extra" "$OUT/kodi/addons/"
    echo "packaged extra add-on: $extra"
  fi
done

# pvr.iptvsimple is a static binary add-on: generated addon.xml, resources, and
# a zero-byte library file that satisfies Kodi's existence check before the shim
# dispatches ADDON_Create.
IPTV_SRC=${KODI_ROOT}/pvr.iptvsimple/pvr.iptvsimple
IPTV_BUILD=${KODI_ROOT}/build-iptv/pvr.iptvsimple
if [ -f "$IPTV_BUILD/addon.xml" ]; then
  DEST="$OUT/kodi/addons/pvr.iptvsimple"
  mkdir -p "$DEST"
  cp -a "$IPTV_BUILD/addon.xml" "$DEST/addon.xml"
  sed -i '/inputstream\.ffmpegdirect/d; /inputstream\.rtmp/d' "$DEST/addon.xml"
  [ -d "$IPTV_SRC/resources" ] && cp -a "$IPTV_SRC/resources" "$DEST/resources"
  [ -f "$IPTV_SRC/icon.png" ] && cp -a "$IPTV_SRC/icon.png" "$DEST/icon.png"
  [ -f "$IPTV_SRC/changelog.txt" ] && cp -a "$IPTV_SRC/changelog.txt" "$DEST/changelog.txt"
  : > "$DEST/pvr.iptvsimple.a"
  # On this port Kodi's ffmpeg inputstream fails on redirect-heavy IPTV HLS
  # URLs; inputstream.adaptive is the working path. Default fresh instances to
  # adaptive while leaving an existing user's saved value alone.
  python3 - "$DEST/resources/settings.xml" "$DEST/resources/instance-settings.xml" <<'PY'
import os, re, sys
for p in sys.argv[1:]:
    if not os.path.exists(p):
        continue
    d = open(p).read()
    d2 = re.sub(r'(<setting id="useInputstreamAdaptiveforHls".*?<default>)false(</default>)',
                r'\1true\2', d, flags=re.S)
    if d2 != d:
        open(p, 'w').write(d2)
        print('patched PVR HLS default: ' + p)
PY
  echo "packaged pvr.iptvsimple"
fi

# inputstream.adaptive is the HLS engine the PVR client needs. Built as a static
# add-on with the same shim convention as pvr.iptvsimple.
ISA_SRC=${KODI_ROOT}/build-isa/inputstream.adaptive
ISA_LIB=${KODI_ROOT}/build-isa/inputstream.adaptive.a
if [ -f "$ISA_SRC/addon.xml" ] && [ -f "$ISA_LIB" ]; then
  DEST="$OUT/kodi/addons/inputstream.adaptive"
  mkdir -p "$DEST"
  cp -a "$ISA_SRC/addon.xml" "$DEST/addon.xml"
  [ -d "$ISA_SRC/resources" ] && cp -a "$ISA_SRC/resources" "$DEST/resources"
  cp -a "$ISA_LIB" "$DEST/inputstream.adaptive.a"
  echo "packaged inputstream.adaptive"
fi

# CPython standard library, for the Python add-on support (PYTHONHOME=/switch/kodi/python).
PYLIB=${KODI_ROOT}/pybuild/Python-3.14.6/Lib
if [ -d "$PYLIB" ]; then
  mkdir -p "$OUT/kodi/python/lib"
  cp -a "$PYLIB" "$OUT/kodi/python/lib/python3.14"
  # Prune what a media centre never needs; saves ~40 MB on the SD card.
  rm -rf "$OUT/kodi/python/lib/python3.14/test" \
         "$OUT/kodi/python/lib/python3.14/idlelib" \
         "$OUT/kodi/python/lib/python3.14/tkinter" \
         "$OUT/kodi/python/lib/python3.14/turtle.py" \
         "$OUT/kodi/python/lib/python3.14/ensurepip" \
         "$OUT/kodi/python/lib/python3.14/lib2to3" \
         "$OUT/kodi/python/lib/python3.14/site-packages" \
         "$OUT/kodi/python/lib/python3.14/config-3.14-aarch64-none-elf"
fi

ls -lh "$OUT/kodi.nro" "$OUT/kodi.elf"
du -sh "$OUT/kodi" "$OUT/kodi/python" 2>/dev/null || true
