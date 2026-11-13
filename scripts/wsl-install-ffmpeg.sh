#!/usr/bin/env bash
set -o pipefail

# Paths (override with environment variables if yours differ)
FFMPEG_BUILD="${FFMPEG_BUILD:-$HOME/ffmpeg-build}"  # ffmpeg source/build scratch dir
KODI_ROOT="${KODI_ROOT:-$HOME/kodi-switch}"      # build tree root (kodi/, build-kodi/, package/)
DEV_DIR="${DEV_DIR:-$HOME/kodi-switch}"         # where these scripts live
SRC_DIR="${SRC_DIR:-$KODI_ROOT}"                # project source root (overlay/, docs/)
SRC=${KODI_ROOT}/portlibs
DST=/opt/devkitpro/portlibs/switch

echo '=== backing up original ffmpeg libs (once) ==='
for f in "$DST"/lib/libav*.a "$DST"/lib/libsw*.a; do
  [ -e "$f" ] || continue
  case "$f" in
    *.bak-*) continue ;;
  esac
  if [ ! -e "$f.bak-orig" ]; then
    sudo cp -a "$f" "$f.bak-orig" && echo "backed up $(basename "$f")"
  fi
done

echo '=== installing new libs ==='
sudo cp -f "$SRC"/lib/libavcodec.a "$DST"/lib/
sudo cp -f "$SRC"/lib/libavformat.a "$DST"/lib/
sudo cp -f "$SRC"/lib/libavutil.a "$DST"/lib/
sudo cp -f "$SRC"/lib/libswscale.a "$DST"/lib/
sudo cp -f "$SRC"/lib/libswresample.a "$DST"/lib/
[ -e "$SRC/lib/libavfilter.a" ] && sudo cp -f "$SRC"/lib/libavfilter.a "$DST"/lib/
[ -e "$SRC/lib/libavdevice.a" ] && sudo cp -f "$SRC"/lib/libavdevice.a "$DST"/lib/

echo '=== installing headers ==='
for d in libavcodec libavformat libavutil libswscale libswresample libavfilter; do
  if [ -d "$SRC/include/$d" ]; then
    sudo rm -rf "$DST/include/$d"
    sudo cp -a "$SRC/include/$d" "$DST/include/"
  fi
done

echo '=== resulting sizes ==='
ls -l "$DST"/lib/libavcodec.a "$DST"/lib/libavformat.a
