#!/bin/sh

FK_FORCE=yes flash-kernel $(find /boot -name 'vmlinuz-*' | sed -e 's|^[^-]*-||')

# Disable Wayland for using Xorg
sed -i 's|#[[:space:]]*WaylandEnable[[:space:]]*=.*|WaylandEnable=false|' /etc/gdm3/custom.conf

chown root:root /etc/udev/rules.d/*
chmod 0644 /etc/udev/rules.d/*
chmod 0755 /usr/bin/merge_dtbo

KELVER=$(find /boot -name 'vmlinuz-*' | sed -e 's|^[^-]*-||')

rsync -av --ignore-existing /usr/src/linux-headers-$KELVER/include/uapi/linux/ /usr/include/linux/

if [ -e /lib/firmware/$KELVER/device-tree/eswin/overlays/ ];then
	cp -rf /lib/firmware/$KELVER/device-tree/eswin/overlays/ /boot/dtbs/$KELVER/eswin/
fi
