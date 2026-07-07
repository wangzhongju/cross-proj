#!/usr/bin/env bash
set -euo pipefail

MODEL=${1:-P550}
WORKSPACE=${WORKSPACE:-/workspace}
SDK_DIR=${SDK_DIR:-$WORKSPACE/eswin-sdk-20250730}
ROOTFS_DIR=${ROOTFS_DIR:-$SDK_DIR/$MODEL/output/rootfs}
DATA_DEB=$(find "$WORKSPACE/packages" -maxdepth 1 -type f -name 'supertuxkart-data_*.deb' | sort | tail -n 1)
MAIN_DEB=$(find "$WORKSPACE/packages" -maxdepth 1 -type f -name 'supertuxkart_[0-9]*.deb' | sort | tail -n 1)

if [ -z "$DATA_DEB" ] || [ -z "$MAIN_DEB" ]; then
  echo "missing supertuxkart debs under $WORKSPACE/packages" >&2
  exit 1
fi

if ! mountpoint -q "$ROOTFS_DIR"; then
  "$WORKSPACE/scripts/chroot_mount.sh" mount "$MODEL"
fi

sudo mkdir -p "$ROOTFS_DIR/root/local-packages"
sudo cp -vf "$DATA_DEB" "$MAIN_DEB" "$ROOTFS_DIR/root/local-packages/"

# Native dpkg resolves statoverride users/groups through the container NSS, so
# mirror any target statoverride groups that are missing in the container.
if [ -f "$ROOTFS_DIR/var/lib/dpkg/statoverride" ]; then
  while read -r group_name; do
    [ -n "$group_name" ] || continue
    if ! getent group "$group_name" >/dev/null 2>&1; then
      sudo groupadd -r "$group_name"
    fi
  done < <(awk '{print $2}' "$ROOTFS_DIR/var/lib/dpkg/statoverride" | sort -u)
fi

# supertuxkart-data is Architecture: all and has no maintainer scripts. Install it
# with native dpkg outside qemu/chroot to avoid qemu-user truncating this 600MB xz payload.
sudo dpkg --root="$ROOTFS_DIR" --force-architecture -i "$DATA_DEB" || true

sudo chroot "$ROOTFS_DIR" /bin/bash -lc '
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive
cat >/usr/sbin/policy-rc.d <<"POLICY_RC_D"
#!/bin/sh
exit 101
POLICY_RC_D
chmod 755 /usr/sbin/policy-rc.d
trap "rm -f /usr/sbin/policy-rc.d" EXIT

sed -i "s/^# *en_US.UTF-8 UTF-8/en_US.UTF-8 UTF-8/" /etc/locale.gen || true
locale-gen en_US.UTF-8 || true
update-locale LANG=en_US.UTF-8 LC_ALL=en_US.UTF-8 || true

apt update
apt install -f -y
apt install -y /root/local-packages/supertuxkart_*.deb
apt install -y libqt5gui5-gles python3-opencv
chown -R eswin:eswin /home/eswin || true
dpkg --configure -a
apt-get check
dpkg -l supertuxkart supertuxkart-data
'

echo "desktop rootfs repaired: $ROOTFS_DIR"
