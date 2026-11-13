#!/usr/bin/env bash
set -euo pipefail

# Paths (override with environment variables if yours differ)
KODI_ROOT="${KODI_ROOT:-$HOME/kodi-switch}"      # build tree root (kodi/, build-kodi/, package/)
DEV_DIR="${DEV_DIR:-$HOME/kodi-switch}"         # where these scripts live
SRC_DIR="${SRC_DIR:-$KODI_ROOT}"                # project source root (overlay/, docs/)

# Create a normal user for builds. The distro is fresh; only the account and
# the Kodi workspace are touched.
if ! id kidbu >/dev/null 2>&1; then
  useradd -m -s /bin/bash kidbu
  echo 'kidbu:kodi' | chpasswd
  usermod -aG sudo kidbu
fi
printf 'kidbu ALL=(ALL) NOPASSWD:ALL\n' > /etc/sudoers.d/90-kidbu
chmod 440 /etc/sudoers.d/90-kidbu

if ! grep -q '^\[user\]' /etc/wsl.conf 2>/dev/null; then
  cat >> /etc/wsl.conf <<'EOF'

[user]
default=kidbu
EOF
fi

mkdir -p ${KODI_ROOT}

# Copy the port overlay and helper scripts.
cp -a "${SRC_DIR}/overlay" ${KODI_ROOT}/
cp -a "${SRC_DIR}/scripts" ${KODI_ROOT}/

# Copy the Kodi checkout into ext4 (do not build over 9p).
if [ ! -d ${KODI_ROOT}/kodi/.git ]; then
  tar -C ${DEV_DIR} -cf - kodi | tar -C ${KODI_ROOT} -xf -
fi

# Apply the platform overlay.
cp -a ${KODI_ROOT}/overlay/. ${KODI_ROOT}/kodi/

# Local cross-built Switch libraries (libiconv for now).
mkdir -p ${KODI_ROOT}/portlibs
cp -a ${DEV_DIR}/portlibs/. ${KODI_ROOT}/portlibs/ 2>/dev/null || true

chown -R kidbu:kidbu ${KODI_ROOT}

echo "Bootstrap done:"
ls -la ${KODI_ROOT}
