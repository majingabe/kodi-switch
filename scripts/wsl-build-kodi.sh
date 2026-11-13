#!/usr/bin/env bash
set -o pipefail

source /etc/profile.d/devkit-env.sh
export PATH="$DEVKITPRO/devkitA64/bin:$DEVKITPRO/tools/bin:$DEVKITPRO/portlibs/switch/bin:$PATH"

PORTLIBS=/home/kidbu/kodi-switch/portlibs
export KODI_SWITCH_PORTLIBS="$PORTLIBS"
export PKG_CONFIG_LIBDIR="$DEVKITPRO/portlibs/switch/lib/pkgconfig:$PORTLIBS/lib/pkgconfig"

cd /home/kidbu/kodi-switch
cmake --build build-kodi -j"$(nproc)" 2>&1 | tee build-kodi.log
