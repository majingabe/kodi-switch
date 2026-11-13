#!/usr/bin/env bash
set -o pipefail

# Paths (override with environment variables if yours differ)
KODI_ROOT="${KODI_ROOT:-$HOME/kodi-switch}"      # build tree root (kodi/, build-kodi/, package/)
DEV_DIR="${DEV_DIR:-$HOME/kodi-switch}"         # where these scripts live
SRC_DIR="${SRC_DIR:-$KODI_ROOT}"                # project source root (overlay/, docs/)

source /etc/profile.d/devkit-env.sh
export PATH="$DEVKITPRO/devkitA64/bin:$DEVKITPRO/tools/bin:$DEVKITPRO/portlibs/switch/bin:$PATH"

PORTLIBS=${KODI_ROOT}/portlibs
export KODI_SWITCH_PORTLIBS="$PORTLIBS"
export PKG_CONFIG_LIBDIR="$DEVKITPRO/portlibs/switch/lib/pkgconfig:$PORTLIBS/lib/pkgconfig"

cd ${KODI_ROOT}
cmake --build build-kodi -j"$(nproc)" 2>&1 | tee build-kodi.log
