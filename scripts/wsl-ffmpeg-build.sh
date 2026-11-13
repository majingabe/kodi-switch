#!/usr/bin/env bash
set -o pipefail

# Paths (override with environment variables if yours differ)
FFMPEG_BUILD="${FFMPEG_BUILD:-$HOME/ffmpeg-build}"  # ffmpeg source/build scratch dir
KODI_ROOT="${KODI_ROOT:-$HOME/kodi-switch}"      # build tree root (kodi/, build-kodi/, package/)
DEV_DIR="${DEV_DIR:-$HOME/kodi-switch}"         # where these scripts live
SRC_DIR="${SRC_DIR:-$KODI_ROOT}"                # project source root (overlay/, docs/)
source /etc/profile.d/devkit-env.sh
export DEVKITPRO
source /opt/devkitpro/switchvars.sh
export PATH="$DEVKITPRO/devkitA64/bin:$DEVKITPRO/tools/bin:$PATH"

cd ${FFMPEG_BUILD}/ffmpeg-7.1 || exit 1
echo "=== building with $(nproc) jobs ==="
make -j"$(nproc)" > ${FFMPEG_BUILD}/make.log 2>&1
rc=$?
echo "make rc=$rc"
if [ $rc -ne 0 ]; then
  tail -30 ${FFMPEG_BUILD}/make.log
  exit 1
fi
echo "=== installing to ${KODI_ROOT}/portlibs ==="
make install > ${FFMPEG_BUILD}/install.log 2>&1
echo "install rc=$?"
ls -l ${KODI_ROOT}/portlibs/lib/libavcodec.a ${KODI_ROOT}/portlibs/lib/libavformat.a 2>/dev/null
