#!/usr/bin/env bash
set -euo pipefail

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

mkdir -p /home/kidbu/kodi-switch

# Copy the port overlay and helper scripts.
cp -a "/mnt/c/Users/kidbu/OneDrive/Documents/Default Project/kodi-switch/overlay" /home/kidbu/kodi-switch/
cp -a "/mnt/c/Users/kidbu/OneDrive/Documents/Default Project/kodi-switch/scripts" /home/kidbu/kodi-switch/

# Copy the Kodi checkout into ext4 (do not build over 9p).
if [ ! -d /home/kidbu/kodi-switch/kodi/.git ]; then
  tar -C /mnt/c/dev/kodi-switch -cf - kodi | tar -C /home/kidbu/kodi-switch -xf -
fi

# Apply the platform overlay.
cp -a /home/kidbu/kodi-switch/overlay/. /home/kidbu/kodi-switch/kodi/

# Local cross-built Switch libraries (libiconv for now).
mkdir -p /home/kidbu/kodi-switch/portlibs
cp -a /mnt/c/dev/kodi-switch/portlibs/. /home/kidbu/kodi-switch/portlibs/ 2>/dev/null || true

chown -R kidbu:kidbu /home/kidbu/kodi-switch

echo "Bootstrap done:"
ls -la /home/kidbu/kodi-switch
