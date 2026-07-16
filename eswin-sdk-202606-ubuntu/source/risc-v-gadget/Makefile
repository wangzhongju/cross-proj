
SERIES ?= noble
DESTDIR ?= install

all:
	mkdir -p $(DESTDIR)
	make install/cidata
	make install/dtb
	make install/grub
	make meta

install/cidata:
	mkdir -p $(DESTDIR)/cidata
	cp user-data $(DESTDIR)/cidata/
	cp meta-data $(DESTDIR)/cidata/
	touch $(DESTDIR)/cidata/vendor-data

install/dtb:
	rm -rf build
	mkdir build
	cp deb-packages/linux-modules*.deb build/
	cd build && dpkg -x linux-modules*.deb linux-modules/
	mkdir -p $(DESTDIR)/dtb
	cp -r ./build/linux-modules/usr/lib/firmware/*-eic7700/device-tree/* $(DESTDIR)/dtb
	rm -rf build

install/grub:
	mkdir -p $(DESTDIR)/grub
	cp grub/grub.cfg $(DESTDIR)/grub/
	cp grub/grubriscv64.efi $(DESTDIR)/grub/

monolithic-grub:
	rm -rf build/
	mkdir build/
	cd build && pull-lp-debs -a riscv64 grub2 '' oracular
	cd build && dpkg -x grub-efi-riscv64-bin*.deb grub/
	# Exclude peimage when building a monolithic GRUB as it would require
	# the NX flag in the PE/COFF header.
	cd build && grub-mkimage -O riscv64-efi -o grubriscv64.efi \
	-p "/EFI/ubuntu" -d grub/usr/lib/grub/riscv64-efi \
	all_video boot btrfs cat chain configfile cryptodisk echo efifwsetup \
	efinet ext2 fat font gcry_arcfour gcry_blowfish gcry_camellia \
	gcry_cast5 gcry_crc gcry_des gcry_dsa gcry_idea gcry_md4 gcry_md5 \
	gcry_rfc2268 gcry_rijndael gcry_rmd160 gcry_rsa gcry_seed gcry_serpent \
	gcry_sha1 gcry_sha256 gcry_sha512 gcry_tiger gcry_twofish \
	gcry_whirlpool gettext gfxmenu gfxterm gfxterm_background gzio halt \
	help hfsplus iso9660 jpeg keystatus linux loadenv loopback ls lsefi \
	lsefimmap lsefisystab lssal luks lvm mdraid09 mdraid1x memdisk minicmd \
	normal ntfs part_apple part_gpt part_msdos password_pbkdf2 png probe \
	raid5rec raid6rec reboot regexp search search_fs_file search_fs_uuid \
	search_label serial sleep smbios squash4 test tpm true video xfs zfs \
	zfscrypt zfsinfo
	cp ./build/grubriscv64.efi grub/

meta:
	mkdir -p $(DESTDIR)/meta
	cp gadget.yaml $(DESTDIR)/meta/

image:
	sudo ubuntu-image classic image-definition.yaml

image-debug:
	sudo rm -rf workdir
	sudo ubuntu-image --workdir workdir --debug classic image-definition.yaml
