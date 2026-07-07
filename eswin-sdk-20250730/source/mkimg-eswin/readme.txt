# 执行过gpt_partition 就可以flash分区
-> in uboot
fastboot usb 0

-> host
fastboot flash boot boot*.ext4
fastboot flash root root*.ext4

-> in boot
boot

就可以进系统了 用户eswin 密码eswin

# mkrootfs.sh
需要在debian环境下运行 

依赖：
sudo apt install -y gdisk dosfstools build-essential autoconf automake autotools-dev ninja-build make \
                                  libncurses-dev gawk flex bison openssl libssl-dev tree \
                                  gcc-riscv64-linux-gnu gfortran-riscv64-linux-gnu libgomp1-riscv64-cross \
                                  qemu-user-static binfmt-support mmdebstrap 
