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
PREFIX=${KODI_ROOT}/portlibs
export PKG_CONFIG_PATH="/opt/devkitpro/portlibs/switch/lib/pkgconfig:$PREFIX/lib/pkgconfig"

CFLAGS_SWITCH='-D__SWITCH__ -D_GNU_SOURCE -O2 -march=armv8-a -mtune=cortex-a57 -mtp=soft -fPIC -ftls-model=local-exec -isystem /opt/devkitpro/libnx/include'

cd ${FFMPEG_BUILD}/ffmpeg-7.1 || exit 1

./configure \
  --prefix=$PREFIX --enable-gpl --disable-shared --enable-static \
  --pkg-config=/usr/bin/pkg-config \
  --cross-prefix=aarch64-none-elf- --enable-cross-compile \
  --arch=aarch64 --cpu=cortex-a57 --target-os=horizon --enable-pic \
  --extra-cflags="$CFLAGS_SWITCH" \
  --extra-cxxflags="$CFLAGS_SWITCH" \
  --extra-ldflags="-fPIE -L$PREFIX/lib -L$DEVKITPRO/libnx/lib -lnx" \
  --disable-runtime-cpudetect --disable-programs --disable-debug --disable-doc --disable-autodetect \
  --enable-asm --enable-neon \
  --disable-avdevice --disable-muxers \
  --disable-encoders --enable-encoder=png,mjpeg \
  --enable-swscale --enable-swresample --enable-network \
  --disable-protocols --enable-protocol=file,http,https,ftp,tcp,udp,rtmp,tls,httpproxy \
  --enable-zlib --enable-libdav1d --enable-libxml2 \
  --enable-libnx --enable-nvtegra \
  > ${FFMPEG_BUILD}/configure.log 2>&1

rc=$?
echo "configure rc=$rc"
if [ $rc -ne 0 ]; then
  grep -iE '^ERROR' ${FFMPEG_BUILD}/configure.log | head -5
  exit 1
fi

echo '=== protocols ==='
sed -n '/Enabled protocols:/,/^$/p' ${FFMPEG_BUILD}/configure.log | head -10
echo '=== encoders ==='
sed -n '/Enabled encoders:/,/^$/p' ${FFMPEG_BUILD}/configure.log | head -6
echo '=== tls / hwaccel ==='
grep -iE 'libnx|nvtegra|https|tls' ${FFMPEG_BUILD}/configure.log | head -12
