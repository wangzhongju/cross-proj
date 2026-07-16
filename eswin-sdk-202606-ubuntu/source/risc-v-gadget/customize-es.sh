#!/bin/sh

export TERM=dumb
export DEBIAN_FRONTEND=noninteractive
export DEBCONF_NONINTERACTIVE_SEEN=true
export DEBCONF_NOWARNINGS=true
export APT_LISTCHANGES_FRONTEND=none
export NEEDRESTART_MODE=a

#apt autoremove linux-image*-eic770*_riscv64.deb
#apt autoreomve linux-modules*-eic770*_riscv64.deb

apt install -y -f --quiet --option=Dpkg::option::=--force-unsafe-io --option=Dpkg::=--force-confold /root/linux-headers*-eic770*_riscv64.deb
apt install -y -f --quiet --option=Dpkg::option::=--force-unsafe-io --option=Dpkg::=--force-confold /root/linux-modules*-eic770*_riscv64.deb
apt install -y -f --quiet --option=Dpkg::option::=--force-unsafe-io --option=Dpkg::=--force-confold /root/linux-image*-eic770*_riscv64.deb

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
cat > /usr/share/initramfs-tools/hooks/no-ai-drivers << 'EOF'
#!/bin/sh

PREREQ=""

prereqs() {
    echo "$PREREQ"
}
case "$1" in
    prereqs)
        prereqs
        exit 0
        ;;
esac

. /usr/share/initramfs-tools/hook-functions

for mod in eic7700_dsp eic7700_npu; do
    find "${DESTDIR}/lib/modules" -name "${mod}.ko" -delete 2>/dev/null
done
EOF

chmod +x /usr/share/initramfs-tools/hooks/no-ai-drivers

update-initramfs -u -k ${KELVER}
