#/bin/bash
if [ "$(id -u)" != "0" ]; then
   echo "Need root permission, you can use sudo to run this script!" 
   exit 1
fi

echo "Current work dir: $(pwd)"

# default using minidump function
KDUMP_DIR="/usr/bin/kdump"
FULL_DUMP=0
KERNEL_IMAGE="minidump/Image"
DTB_FILE="eic7702-d560.dtb"
APPEND_ARGS="console=ttyS0,115200 earlycon init=/init rootfstype=ramfs rootwait maxcpus=1 reset_devices"
SHOW_HELP=false

cd "$KDUMP_DIR" || { echo "has not switch to dir: $KDUMP_DIR" ; exit 1; }
echo "switch to kdump dir: $(pwd)"

# Show help information
show_help() {
    cat <<EOF
Usage: $0 [OPTIONS]

Kdump/kexec preload tool

Options:
  -f, --fulldump        Specify fulldump otherwise minidump (default: minidump(FULL_DUMP=0))
  -k, --kernel FILE     Specify the kernel image file (default: $KERNEL_IMAGE)
  -d, --dtb FILE        Specify DTB file (default: $DTB_FILE)
  -a, --append PARAMS   Specify additional parameters for kernel startup (default: "$APPEND_ARGS")
  -h, --help            Show this help information
EOF
}

KERNEL_SPECIFIED=false
DTB_SPECIFIED=false
# Parse command-line parameters
while [[ $# -gt 0 ]]; do
    case "$1" in
        -f|--fulldump)
            FULL_DUMP=1
            shift 1
            ;;
        -k|--kernel)
            KERNEL_IMAGE="$2"
            KERNEL_SPECIFIED=true
            shift 2
            ;;
        -d|--dtb)
            DTB_FILE="$2"
            DTB_SPECIFIED=true
            shift 2
            ;;
        -a|--append)
            APPEND_ARGS="$2"
            shift 2
            ;;
        -h|--help)
            SHOW_HELP=true
            shift
            ;;
        *)
            echo "error: Unknown option $1"
            show_help
            exit 1
            ;;
    esac
done

if $SHOW_HELP; then
    show_help
    exit 0
fi

# FULL_DUMP: auto add prefix
if [ $FULL_DUMP -eq 1 ]; then
    DUMP_TYPE="fulldump"
    echo "Use the complete dump mode (fulldump)"
else
    DUMP_TYPE="minidump"
    echo "Use the minimum dump mode (minidump)"
fi

# check and add kernel image dir prefix
if ! $KERNEL_SPECIFIED && [[ ! $KERNEL_IMAGE == /* ]] && [[ ! $KERNEL_IMAGE == */$DUMP_TYPE/* ]]; then
    KERNEL_IMAGE="$DUMP_TYPE/Image"
    echo "auto add kernel image prefix: $KERNEL_IMAGE"
fi

# Verify the existence of the file
check_file_exists() {
    local file=$1
    local type=$2
    if [ ! -f "$file" ]; then
        echo "error: $type file not exist: $file" >&2
        exit 1
    fi
    echo "find $type file: $file"
}

check_file_exists "$KERNEL_IMAGE" "Kernel"
check_file_exists "$DTB_FILE" "DTB"

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
KEXEC_CMD=(./kexec -p "$KERNEL_IMAGE" \
    --dtb="$DTB_FILE" \
    --append="$APPEND_ARGS" \
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
