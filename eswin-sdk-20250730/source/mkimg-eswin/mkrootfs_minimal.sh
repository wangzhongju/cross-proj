#!/usr/bin/env bash

BOOT_SIZE=100M
BOOT_IMG=""
ROOT_SIZE=1000M
ROOT_IMG=""
CHROOT_TARGET=rootfs
#BOOT_UUID="44b7cb94-f58c-4ba6-bfa4-7d2dce09a3a5"
#ROOT_UUID="80a5a8e9-c744-491a-93c1-4f4194fd690a"
BOARD=$1
RELEASE_TAG=$2
KDUMP=$3
ker_ver=`echo $RELEASE_TAG | tr "A-Z" "a-z"`

if [ -f ../output/linux-image-*-dbg*.deb ];then
    echo debug kernel
    DEBUG_KERNEL_DEB="linux-image-6.6.18-${ker_ver}-dbg"
    ROOT_SIZE=4G
fi

TIMESTAMP=$(date +%Y%m%d-%H%M%S)
echo $TIMESTAMP  >../TIMESTAMP

make_imagefile()
{
    BOOT_IMG="boot-$BOARD-$TIMESTAMP.ext4"
    truncate -s "$BOOT_SIZE" "$BOOT_IMG"
    ROOT_IMG="root-$BOARD-$TIMESTAMP.ext4"
    truncate -s "$ROOT_SIZE" "$ROOT_IMG"

    # Format partitions
    mkfs.ext4 -F -O ^metadata_csum "$BOOT_IMG"
    mkfs.ext4 -F -O ^metadata_csum "$ROOT_IMG"

    export BOOT_UUID=$(blkid $BOOT_IMG| awk -F'UUID="|"' '{print $2}')
    export ROOT_UUID=$(blkid $ROOT_IMG| awk -F'UUID="|"' '{print $2}')
    # UUID
    #tune2fs -U $BOOT_UUID $BOOT_IMG
    #tune2fs -U $ROOT_UUID $ROOT_IMG
}

pre_mkrootfs()
{
    # Mount loop device
    mkdir -p "$CHROOT_TARGET"
    mount "$ROOT_IMG" "$CHROOT_TARGET"
}

unmount_image()
{
	echo "Finished and cleaning..."
	if mount | grep "$CHROOT_TARGET" > /dev/null; then
		umount -l "$CHROOT_TARGET"
	fi
	if [ "$(ls -A $CHROOT_TARGET)" ]; then
		echo "folder not empty! umount may fail!"
		exit 2
	else
		echo "Deleting chroot temp folder..."
		if [ -d "$CHROOT_TARGET" ]; then
			rmdir -v "$CHROOT_TARGET"
		fi
		echo "Done."
	fi
}

make_rootfs_tarball()
{
    # use $1
#    PACKAGE_LIST="$KEYRINGS $GPU_DRIVER $BASE_TOOLS $GRAPHIC_TOOLS $XFCE_DESKTOP $BENCHMARK_TOOLS $FONTS $INCLUDE_APPS $EXTRA_TOOLS $LIBREOFFICE"
    PACKAGE_LIST="ca-certificates cloud-guest-utils network-manager wpasupplicant rockos-keyring u-boot-menu sudo initramfs-tools systemd-timesyncd arch-install-scripts"
    mmdebstrap --architectures=riscv64 \
        --include="$PACKAGE_LIST" \
        --skip check/empty \
        sid $1 \
        "deb [trusted=yes] https://mirror.iscas.ac.cn/rockos/20250730/rockos-gles/ rockos-gles main" \
        "deb [trusted=yes] https://mirror.iscas.ac.cn/rockos/20250730/rockos-kernels/ rockos-kernels main" \
        "deb [trusted=yes] https://mirror.iscas.ac.cn/rockos/20250730/rockos-addons/ rockos-addons main" \
        "deb [trusted=yes] https://mirror.iscas.ac.cn/rockos/20250730/rockos-base/ sid main contrib non-free non-free-firmware"
}

make_rootfs()
{
    mkdir -p $CHROOT_TARGET/tmp
    mount -t tmpfs tmpfs "$CHROOT_TARGET"/tmp
    mkdir -p $CHROOT_TARGET/var/tmp
    mount -t tmpfs tmpfs "$CHROOT_TARGET"/var/tmp
    mkdir -p $CHROOT_TARGET/var/cache
    mount -t tmpfs tmpfs "$CHROOT_TARGET"/var/cache
    make_rootfs_tarball $CHROOT_TARGET
    umount "$CHROOT_TARGET"/var/cache
    umount "$CHROOT_TARGET"/var/tmp
    umount "$CHROOT_TARGET"/tmp
    if [ ! -z "$(ls -A "$CHROOT_TARGET"/boot/)" ]; then
        mkdir "$CHROOT_TARGET"/mnt/boot
        mv -v "$CHROOT_TARGET"/boot/* "$CHROOT_TARGET"/mnt/boot/
    fi

    # Mount chroot path
    mflag=1
    while [ $mflag != 0 ];
    do
        mkdir -p "$CHROOT_TARGET"/boot
        mount "$BOOT_IMG" "$CHROOT_TARGET"/boot
        mountpoint -q "$CHROOT_TARGET"/boot
        mflag=$?
        if [ $mflag != 0 ];then
            echo "Mount failure"
            echo "The system has reached the upper limit of available loop devices."
            echo "You can run the 'losetup -a' command to view the current loop devices on the host."
            echo "You can do this by 'sudo losetup -d /dev/loopX' (replacing X with the actual loop device number)Try to uninstall a device that is no longer in use."
            echo "Automatically remount after 60 seconds."
            sleep 60
        fi
    done
    mount -t proc /proc "$CHROOT_TARGET"/proc
    cat > "$CHROOT_TARGET"/usr/sbin/policy-rc.d <<'POLICY_RC_D'
#!/bin/sh
exit 101
POLICY_RC_D
    chmod 755 "$CHROOT_TARGET"/usr/sbin/policy-rc.d

    mount -B /sys "$CHROOT_TARGET"/sys
    mount -B /run "$CHROOT_TARGET"/run
    mount -B /dev "$CHROOT_TARGET"/dev
    mount -B /dev/pts "$CHROOT_TARGET"/dev/pts
    mount -t tmpfs tmpfs "$CHROOT_TARGET"/tmp
    mount -t tmpfs tmpfs "$CHROOT_TARGET"/var/tmp
    mount -t tmpfs tmpfs "$CHROOT_TARGET"/var/cache

    # move boot contents back to /boot
    if [ ! -z "$(ls -A "$CHROOT_TARGET"/mnt/boot/)" ]; then
        mv -v "$CHROOT_TARGET"/mnt/boot/* "$CHROOT_TARGET"/boot/
        rmdir "$CHROOT_TARGET"/mnt/boot
    fi

    # apt update
    chroot "$CHROOT_TARGET" sh -c "apt update"
    chroot "$CHROOT_TARGET" sh -c "sed -i 's/^# *en_US.UTF-8 UTF-8/en_US.UTF-8 UTF-8/' /etc/locale.gen && locale-gen en_US.UTF-8 && update-locale LANG=en_US.UTF-8 LC_ALL=en_US.UTF-8" || true
}

make_bootable()
{
    # Install kernel
    deb_package=$(ls -l ../output/ | grep "riscv64.deb")
    if [ "$deb_package" != "" ];then
        cp -v ../output/*.deb $CHROOT_TARGET/root/
        chroot "$CHROOT_TARGET" sh -c 'dpkg -i /root/*.deb'
        chroot "$CHROOT_TARGET" sh -c 'apt install -f -y'
        echo "kernel debs installed from ../output/*.deb; skip hard-coded linux-image-6.6.18 install"
    fi
    #chroot "$CHROOT_TARGET" sh -c 'apt update && apt install -y linux-image-6.6.36-win2030 eic770x-firmware'

    # Add update-u-boot config
    if [[ -n "${KDUMP}" && "${KDUMP}" == "kdump" ]]; then
    cat > $CHROOT_TARGET/etc/default/u-boot << EOF
U_BOOT_PROMPT="2"
U_BOOT_MENU_LABEL="RockOS GNU/Linux"
U_BOOT_PARAMETERS="console=tty0 console=ttyS0,115200 rootfstype=ext4 rootwait rw earlycon selinux=0 panic=5 reboot=warm crashkernel=512M LANG=en_US.UTF-8 audit=0"
U_BOOT_ROOT="root=UUID=${ROOT_UUID}"
U_BOOT_FDT_DIR="/dtbs/linux-image-"
EOF
    else
    cat > $CHROOT_TARGET/etc/default/u-boot << EOF
U_BOOT_PROMPT="2"
U_BOOT_MENU_LABEL="RockOS GNU/Linux"
U_BOOT_PARAMETERS="console=ttyS0,115200 rootfstype=ext4 rootwait rw earlycon selinux=0 panic=5 reboot=warm LANG=en_US.UTF-8 audit=0"
U_BOOT_ROOT="root=UUID=${ROOT_UUID}"
U_BOOT_FDT_DIR="/dtbs/linux-image-"
EOF
    fi

    # Update extlinux config
    sed -i "s/single/bootmode=recovery single/g" "$CHROOT_TARGET"/usr/sbin/u-boot-update
    chroot "$CHROOT_TARGET" sh -c "u-boot-update"
}

after_mkrootfs()
{
    # Set default timezone to Asia/Shanghai
    chroot "$CHROOT_TARGET" sh -c "ln -sf /usr/share/zoneinfo/Asia/Shanghai /etc/localtime"
    echo "Asia/Shanghai" > $CHROOT_TARGET/etc/timezone

    # Set up fstab
    chroot $CHROOT_TARGET /bin/bash << EOF
echo 'UUID=${ROOT_UUID} /   auto    defaults,x-systemd.growfs    1 1' >> /etc/fstab
echo 'UUID=${BOOT_UUID} /boot   auto    defaults,x-systemd.growfs    0 0' >> /etc/fstab

exit
EOF

    # Add user
    chroot "$CHROOT_TARGET" sh -c "useradd -m -s /bin/bash -G adm,cdrom,floppy,sudo,input,audio,dip,video,plugdev,netdev eswin"
    chroot "$CHROOT_TARGET" sh -c "echo 'eswin:eswin' | chpasswd"

    # Change hostname
    chroot $CHROOT_TARGET /bin/bash << EOF
echo rockos-eswin > /etc/hostname
echo "127.0.1.1 rockos-eswin" >> /etc/hosts
exit
EOF
    

    #firmware
    mkdir -p "$CHROOT_TARGET"/lib/firmware
    cp -rf firmware/* "$CHROOT_TARGET"/lib/firmware/
    # enable firstboot
    cp -vf addons/opt/firstboot.sh "$CHROOT_TARGET"/opt
    cp -vf addons/opt/firstboot.service "$CHROOT_TARGET"/etc/systemd/system/firstboot.service
    chroot "$CHROOT_TARGET" sh -c "systemctl enable firstboot"

    # add udevs rules
    cp -vf rules/* "$CHROOT_TARGET"/etc/udev/rules.d/ 
    sed -i '/SUBSYSTEMS=="platform", ENV{SOUND_FORM_FACTOR}="internal".*/d' "$CHROOT_TARGET"/usr/lib/udev/rules.d/78-sound-card.rules

    #bin
    if [ -d bin ];then
        cp -vf bin/* "$CHROOT_TARGET"/usr/bin/
    fi

    #lib
    if [ -d lib ];then
        cp -vf lib/* "$CHROOT_TARGET"/usr/lib/
    fi

    #P550 
    if [ "$BOARD" == "P550" ] || [ "$BOARD" == "EIC7700-02-1154B1" ];then
        cp -vf bmc/es-bmcd "$CHROOT_TARGET"/usr/bin/
	cp -vf bmc/98-es-bmcd.preset "$CHROOT_TARGET"/usr/lib/systemd/system-preset/
	cp -vf bmc/es-bmcd.sh "$CHROOT_TARGET"/usr/bin/es-bmcd.sh
	cp -vf bmc/es-bmcd.service "$CHROOT_TARGET"/usr/lib/systemd/system
	chroot "$CHROOT_TARGET" sh -c "systemctl enable es-bmcd.service"
    fi

    #es fan
    cp -vf fan/es-fand "$CHROOT_TARGET"/usr/bin/
    cp -vf fan/98-es-fand.preset "$CHROOT_TARGET"/usr/lib/systemd/system-preset/
    cp -vf fan/es-fand.sh "$CHROOT_TARGET"/usr/bin/
    cp -vf fan/es-fand.service "$CHROOT_TARGET"/usr/lib/systemd/system/
    cp -vf fan/es-fand.conf "$CHROOT_TARGET"/etc/
    chroot "$CHROOT_TARGET" sh -c "systemctl enable es-fand.service"

    #es kdump
    if [[ -n "${KDUMP}" && "${KDUMP}" == "kdump" ]]; then
        mkdir "$CHROOT_TARGET"/usr/bin/kdump
        cp -vf capture_kernel/kexec "$CHROOT_TARGET"/usr/bin/kdump
        if [ "$BOARD" == "EIC7700FG-EVB-LP5-A2" ];then
            cp -vrf capture_kernel/eic7700/* "$CHROOT_TARGET"/usr/bin/kdump
        elif [ "$BOARD" == "EIC7702-E-L5G5" ];then
            cp -vrf capture_kernel/eic7702/* "$CHROOT_TARGET"/usr/bin/kdump
        fi
    fi

    #es release
    chroot $CHROOT_TARGET /bin/bash << EOF
echo $BOARD:$RELEASE_TAG> /etc/es_release
exit
EOF
}

make_imagefile
pre_mkrootfs
make_rootfs
make_bootable
after_mkrootfs
rm -f "$CHROOT_TARGET"/usr/sbin/policy-rc.d
unmount_image
