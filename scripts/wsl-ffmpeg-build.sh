#!/usr/bin/env bash
set -o pipefail
source /etc/profile.d/devkit-env.sh
export DEVKITPRO
source /opt/devkitpro/switchvars.sh
export PATH="$DEVKITPRO/devkitA64/bin:$DEVKITPRO/tools/bin:$PATH"

cd /home/kidbu/ffmpeg-build/ffmpeg-7.1 || exit 1
echo "=== building with $(nproc) jobs ==="
make -j"$(nproc)" > /home/kidbu/ffmpeg-build/make.log 2>&1
rc=$?
echo "make rc=$rc"
if [ $rc -ne 0 ]; then
  tail -30 /home/kidbu/ffmpeg-build/make.log
  exit 1
fi
echo "=== installing to /home/kidbu/kodi-switch/portlibs ==="
make install > /home/kidbu/ffmpeg-build/install.log 2>&1
echo "install rc=$?"
ls -l /home/kidbu/kodi-switch/portlibs/lib/libavcodec.a /home/kidbu/kodi-switch/portlibs/lib/libavformat.a 2>/dev/null
