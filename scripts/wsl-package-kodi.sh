#!/usr/bin/env bash
set -euo pipefail

BUILD=/home/kidbu/kodi-switch/build-kodi
SRC=/home/kidbu/kodi-switch/kodi
OUT=/home/kidbu/kodi-switch/package
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

# CPython standard library, for the Python add-on support (PYTHONHOME=/switch/kodi/python).
PYLIB=/home/kidbu/kodi-switch/pybuild/Python-3.14.6/Lib
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
