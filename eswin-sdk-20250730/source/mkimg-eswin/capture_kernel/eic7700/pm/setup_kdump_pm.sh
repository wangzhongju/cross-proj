#/bin/bash
if [ "$(id -u)" != "0" ]; then
   echo "Need root permission, you can use sudo to run this script!" 
   exit 1
fi

echo "Current work dir: $(pwd)"

KDUMP_DIR="/usr/bin/kdump"
cd "$KDUMP_DIR" || { echo "has not switch to dir: $KDUMP_DIR" ; exit 1; }
echo "switch to kdump dir: $(pwd)"

echo "check Crash kernel reserved mem..."
CRASH_KERNEL=$(cat /proc/iomem | grep "Crash kernel")
if [[ -z "$CRASH_KERNEL" ]]; then
    echo "can't find Crash kernel reserved mem"
    exit 1
fi
echo "Crash kernel: $CRASH_KERNEL"

MEM_MIN=$(echo "$CRASH_KERNEL" | awk '{print $1}' | awk -F'-' '{print $1}')
echo "Crash kernel reserved mem start address: $MEM_MIN"

echo "do kexec preload..."
KEXEC_CMD=(./kexec -p pm/Image \
    --dtb="pm/eic7700-evb-a2.dtb" \
    --append="console=ttyS0,115200 earlycon init=/init rootfstype=ramfs rootwait maxcpus=1 reset_devices" \
    --mem-min=0x"$MEM_MIN")

echo "do cmd: ${KEXEC_CMD[*]}"
if "${KEXEC_CMD[@]}"; then
    echo -e "\e[32mkexec preload success!!!\e[0m"
    echo 1 > /proc/sys/kernel/panic_on_rcu_stall
    echo 1 > /proc/sys/kernel/max_rcu_stall_to_panic
    printf "panic_on_rcu_stall\t = %s\n" "$(cat /proc/sys/kernel/panic_on_rcu_stall)"
    printf "max_rcu_stall_to_panic\t = %s\n" "$(cat /proc/sys/kernel/max_rcu_stall_to_panic)"
    echo -e "\e[32msetup RCU stall trigger panic!!!\e[0m"
else
    echo "kexec preload fail!!!"
    exit 1
fi
