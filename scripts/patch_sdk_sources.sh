#!/usr/bin/env bash
set -euo pipefail

WORKSPACE=${WORKSPACE:-/workspace}
SDK_DIR=${SDK_DIR:-$WORKSPACE/eswin-sdk-20250730}
UBOOT_DIR=${UBOOT_DIR:-$SDK_DIR/source/uboot-eswin}

patch_uboot() {
  local uboot_dir=$1
  if [ ! -d "$uboot_dir" ]; then echo "u-boot source not found, skip: $uboot_dir" >&2; return 0; fi
  python3 - "$uboot_dir" <<'PY'
from pathlib import Path
import sys
uboot = Path(sys.argv[1])
bootloader = uboot / 'cmd/eswin/es_bootloader.c'
if bootloader.exists():
    s = bootloader.read_text()
    s = s.replace('void defrag_move_cb(void *user_data, defrag_info_t *info)', 'void defrag_move_cb(void *user_data, const defrag_info_t *info)')
    s = s.replace('uint32_t crc_src = crc32(0, &src_flash_info->num_entries, size);', 'uint32_t crc_src = crc32(0, (const unsigned char *)&src_flash_info->num_entries, size);')
    s = s.replace('uint32_t crc_dst = crc32(0, &dst_flash_info->num_entries, size);', 'uint32_t crc_dst = crc32(0, (const unsigned char *)&dst_flash_info->num_entries, size);')
    s = s.replace('es_write_bootchain(src_flash_info, BOOTLOAD_INFO_OFFSET, len);', 'es_write_bootchain((uint64_t)src_flash_info, BOOTLOAD_INFO_OFFSET, len);')
    bootloader.write_text(s)
splash = uboot / 'common/splash.c'
if splash.exists():
    s = splash.read_text()
    needle = '#include <dm/device.h>\n'
    if '#include <dm/device-internal.h>' not in s:
        s = s.replace(needle, needle + '#include <dm/device-internal.h>\n')
    splash.write_text(s)
PY
  echo "u-boot patches applied: $uboot_dir"
}

patch_kernel() {
  local kernel_dir=$1
  if [ ! -d "$kernel_dir" ]; then echo "kernel source not found, skip: $kernel_dir" >&2; return 0; fi
  python3 - "$kernel_dir" <<'PY'
from pathlib import Path
import sys
kernel = Path(sys.argv[1])
main = kernel / 'drivers/soc/eswin/ai_driver/dsp/dsp_main.c'
if main.exists():
    s = main.read_text()
    if '#include <linux/eswin-win2030-sid-cfg.h>' not in s:
        s = s.replace('#include <linux/pm_opp.h>\n', '#include <linux/pm_opp.h>\n#include <linux/eswin-win2030-sid-cfg.h>\n')
    main.write_text(s)
header = kernel / 'drivers/soc/eswin/ai_driver/dsp/dsp_platform.h'
if header.exists():
    s = header.read_text()
    needle = 'int es_dsp_get_subsys(struct platform_device *pdev, struct es_dsp *dsp);\n'
    extra = needle + 'void es_dsp_put_subsys(struct es_dsp *dsp);\nint es_dsp_map_resource(struct es_dsp *dsp);\nint es_dsp_unmap_resource(struct es_dsp *dsp);\n'
    if 'int es_dsp_map_resource(struct es_dsp *dsp);' not in s and needle in s:
        s = s.replace(needle, extra)
    header.write_text(s)
defconfig = kernel / 'arch/riscv/configs/eic7700_defconfig'
if defconfig.exists():
    lines = [line for line in defconfig.read_text().splitlines() if not line.startswith('CONFIG_KVM=') and not line.startswith('# CONFIG_KVM is not set')]
    lines.append('# CONFIG_KVM is not set')
    defconfig.write_text('\n'.join(lines) + '\n')
loader = kernel / 'drivers/soc/eswin/ai_driver/dsp/mloader/xt_mld_loader.c'
if loader.exists():
    s = loader.read_text()
    s = s.replace('xtmld_ptr ret = ((((uint32_t)ptr + align_adj) & ~(align - 1)) + offset);', 'xtmld_ptr ret = (xtmld_ptr)(unsigned long)((((uint32_t)(unsigned long)ptr + align_adj) & ~(align - 1)) + offset);')
    s = s.replace('xtmld_ptr ret_hi = (uint64_t)ptr & ADDR64_HI;', 'unsigned long ret_hi = (unsigned long)ptr & ADDR64_HI;')
    s = s.replace('ret = (xtmld_ptr)((uint64_t)ret_hi | (uint64_t)ret);', 'ret = (xtmld_ptr)(ret_hi | (unsigned long)ret);')
    loader.write_text(s)
logo = kernel / 'drivers/video/logo/Makefile'
if logo.exists():
    s = logo.read_text()
    if 'HOSTCFLAGS_pnmtologo.o += -O0' not in s:
        s = s.replace('hostprogs := pnmtologo\n', 'hostprogs := pnmtologo\nHOSTCFLAGS_pnmtologo.o += -O0\n')
    logo.write_text(s)
platform = kernel / 'drivers/soc/eswin/ai_driver/dsp/dsp_platform.c'
if platform.exists():
    s = platform.read_text()
    if '#include <linux/clk-provider.h>' not in s:
        s = s.replace('#include <linux/clk.h>\n', '#include <linux/clk.h>\n#include <linux/clk-provider.h>\n')
    platform.write_text(s)
ioctl = kernel / 'drivers/soc/eswin/ai_driver/dsp/dsp_ioctl.c'
if ioctl.exists():
    s = ioctl.read_text()
    s = s.replace('user_req->callback = task->task.callback;', 'user_req->callback = (u64)task->task.callback;')
    s = s.replace('user_req->cbarg = task->task.cbArg;', 'user_req->cbarg = (u64)task->task.cbArg;')
    s = s.replace('retval = dsp_ioctl_unload_op(flip, arg);', 'retval = dsp_ioctl_unload_op(flip, (void __user *)arg);')
    s = s.replace('es_dsp_pm_put_sync(dsp->dev);', 'es_dsp_pm_put_sync(dsp);')
    ioctl.write_text(s)
PY
  echo "kernel patches applied: $kernel_dir"
}

patch_mkimg() {
  local mkimg_dir=$1
  if [ ! -d "$mkimg_dir" ]; then echo "mkimg source not found, skip: $mkimg_dir" >&2; return 0; fi
  python3 - "$mkimg_dir" <<'PY'
from pathlib import Path
import sys
mkimg = Path(sys.argv[1])

def patch_common(path):
    if not path.exists(): return
    s = path.read_text()
    s = s.replace("chroot \"$CHROOT_TARGET\" sh -c 'apt install -f'", "chroot \"$CHROOT_TARGET\" sh -c 'apt install -f -y'")
    s = s.replace('    mkdir "$CHROOT_TARGET"\n    mount "$ROOT_IMG" "$CHROOT_TARGET"', '    mkdir -p "$CHROOT_TARGET"\n    mount "$ROOT_IMG" "$CHROOT_TARGET"')
    s = s.replace('        mount "$BOOT_IMG" "$CHROOT_TARGET"/boot\n', '        mkdir -p "$CHROOT_TARGET"/boot\n        mount "$BOOT_IMG" "$CHROOT_TARGET"/boot\n')
    while '        mkdir -p "$CHROOT_TARGET"/boot\n        mkdir -p "$CHROOT_TARGET"/boot\n' in s:
        s = s.replace('        mkdir -p "$CHROOT_TARGET"/boot\n        mkdir -p "$CHROOT_TARGET"/boot\n', '        mkdir -p "$CHROOT_TARGET"/boot\n')
    s = s.replace('chroot "$CHROOT_TARGET" sh -c "apt update && apt install -y linux-image-6.6.18-${ker_ver} ${DEBUG_KERNEL_DEB} linux-headers-6.6.18-${ker_ver}"', 'echo "kernel debs installed from ../output/*.deb; skip hard-coded linux-image-6.6.18 install"')
    s = s.replace('chroot "$CHROOT_TARGET" sh -c "apt update && apt install -y linux-image-6.6.18-${ker_ver} $DEBUG_KERNEL_DEB linux-headers-6.6.18-${ker_ver}"', 'echo "kernel debs installed from ../output/*.deb; skip hard-coded linux-image-6.6.18 install"')
    # Avoid locale warnings before after_mkrootfs() runs.
    early_locale = '    # apt update\n    chroot "$CHROOT_TARGET" sh -c "apt update"\n'
    early_locale_repl = """    # apt update
    chroot "$CHROOT_TARGET" sh -c "apt update"
    chroot "$CHROOT_TARGET" sh -c "sed -i 's/^# *en_US.UTF-8 UTF-8/en_US.UTF-8 UTF-8/' /etc/locale.gen && locale-gen en_US.UTF-8 && update-locale LANG=en_US.UTF-8 LC_ALL=en_US.UTF-8" || true
"""
    if early_locale in s and 'update-locale LANG=en_US.UTF-8 LC_ALL=en_US.UTF-8' not in s:
        s = s.replace(early_locale, early_locale_repl)

    # Chroot package installs should not try to start/reload host services.
    proc_mount = '    mount -t proc /proc "$CHROOT_TARGET"/proc\n'
    policy_block = """    cat > "$CHROOT_TARGET"/usr/sbin/policy-rc.d <<'POLICY_RC_D'
#!/bin/sh
exit 101
POLICY_RC_D
    chmod 755 "$CHROOT_TARGET"/usr/sbin/policy-rc.d

"""
    if proc_mount in s and 'POLICY_RC_D' not in s:
        s = s.replace(proc_mount, proc_mount + policy_block)
    if 'rm -f "$CHROOT_TARGET"/usr/sbin/policy-rc.d' not in s:
        s = s.replace('    echo load-module module-bluez5-discover        >>"$CHROOT_TARGET"/etc/pulse/default.pa\n', '    echo load-module module-bluez5-discover        >>"$CHROOT_TARGET"/etc/pulse/default.pa\n    rm -f "$CHROOT_TARGET"/usr/sbin/policy-rc.d\n')

    s = s.replace('after_mkrootfs\nunmount_image', 'after_mkrootfs\nrm -f "$CHROOT_TARGET"/usr/sbin/policy-rc.d\nunmount_image')

    # Empty home directory should not make the image build look failed.
    s = s.replace("    chroot \"$CHROOT_TARGET\" sh -c 'chown eswin:eswin /home/eswin/*'",
                  "    chroot \"$CHROOT_TARGET\" sh -c 'chown -R eswin:eswin /home/eswin || true'")

    path.write_text(s)

def patch_desktop(path):
    if not path.exists(): return
    s = path.read_text()
    desktop_block = """    # prepare local supertuxkart debs from /workspace/packages
    if [ -d "${WORKSPACE:-/workspace}/packages" ] && ls "${WORKSPACE:-/workspace}"/packages/supertuxkart*.deb >/dev/null 2>&1; then
        mkdir -p "$CHROOT_TARGET"/root/local-packages
        cp -vf "${WORKSPACE:-/workspace}"/packages/supertuxkart*.deb "$CHROOT_TARGET"/root/local-packages/
    fi

    # xfce desktop
    chroot $CHROOT_TARGET /bin/bash << EOF
set -e
export DEBIAN_FRONTEND=noninteractive
apt install -y task-xfce-desktop
apt install -y qemu-system
apt install -y bash-completion
apt install -y firmware-amd-graphics
apt install -y mesa-vulkan-drivers mesa-va-drivers mesa-vdpau-drivers
apt install -y ssh
apt install -y glmark2-es2 mesa-utils vulkan-tools
if ls /root/local-packages/supertuxkart*.deb >/dev/null 2>&1; then
    dpkg -i /root/local-packages/supertuxkart-data_*.deb || apt install -f -y
    apt install -y /root/local-packages/supertuxkart_*.deb
else
    apt install -y supertuxkart
fi
exit
EOF

"""
    start = s.find('    # prepare local supertuxkart debs from /workspace/packages\n')
    if start == -1:
        start = s.find('    # xfce desktop\n')
    end = s.find('    # fix pulseaudio\n', start if start != -1 else 0)
    if start != -1 and end != -1:
        s = s[:start] + desktop_block + s[end:]
    old = '    cat << EOF > "$CHROOT_TARGET"/etc/powervr.ini\n# supertuxkart is skipped because supertuxkart-data from the EIC7X-2025.07 mirror is corrupt.\nEOF\n'
    new = '    cat << EOF > "$CHROOT_TARGET"/etc/powervr.ini\n[supertuxkart]\nDisableFBCDC=1\nEOF\n'
    s = s.replace(old, new)
    path.write_text(s)

for name in ('mkrootfs.sh', 'mkrootfs_minimal.sh'):
    patch_common(mkimg / name)
patch_desktop(mkimg / 'mkrootfs.sh')
PY
  echo "mkimg patches applied: $mkimg_dir"
}

patch_uboot "$UBOOT_DIR"
patch_kernel "$SDK_DIR/source/linux-eswin"
patch_mkimg "$SDK_DIR/source/mkimg-eswin"

for copied_kernel in "$SDK_DIR"/*/linux-eswin; do
  [ -d "$copied_kernel" ] || continue
  patch_kernel "$copied_kernel"
done
for copied_mkimg in "$SDK_DIR"/*/mkimg-eswin; do
  [ -d "$copied_mkimg" ] || continue
  patch_mkimg "$copied_mkimg"
done
