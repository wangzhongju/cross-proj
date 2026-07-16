#!/bin/bash
export WORK_DIR=`pwd`
export PATH="/opt/riscv/bin:$PATH"
export ARCH=riscv
export CROSS_COMPILE=riscv64-unknown-linux-gnu-
export RELEASE_TAG=EIC7X-26.06
function make_bootchain()
{
    echo "start compile bootchain"
    #echo "ddr name:$ddr_name"
    echo "dts name:$board_name"
    mkdir -p ${WORK_DIR}/$board_name
    rm -rf ${WORK_DIR}/$board_name/uboot-eswin${FEATURE}
    rm -rf ${WORK_DIR}/$board_name/opensbi-eswin${FEATURE}
    rm -rf ${WORK_DIR}/$board_name/firmware-eswin${FEATURE}
    if [ ! -d $WORK_DIR/source/uboot-eswin${FEATURE} ];then
        git clone -b ${RELEASE_TAG} https://github.com/eswincomputing/u-boot.git $WORK_DIR/source/uboot-eswin${FEATURE}
    fi
    rsync -au --chmod=u=rwX,go=rX  --exclude .git --exclude .hg --exclude .bzr --exclude CVS $WORK_DIR/source/uboot-eswin${FEATURE} ${WORK_DIR}/${board_name}/
    if [ ! -d $WORK_DIR/source/opensbi-eswin${FEATURE} ];then
        git clone -b ${RELEASE_TAG} https://github.com/eswincomputing/opensbi.git $WORK_DIR/source/opensbi-eswin${FEATURE}
    fi
    rsync -au --chmod=u=rwX,go=rX  --exclude .git --exclude .hg --exclude .bzr --exclude CVS $WORK_DIR/source/opensbi-eswin${FEATURE} ${WORK_DIR}/${board_name}/
    rsync -au --chmod=u=rwX,go=rX  --exclude .git --exclude .hg --exclude .bzr --exclude CVS $WORK_DIR/source/firmware-eswin ${WORK_DIR}/${board_name}/
    #uboot
    cd ${WORK_DIR}/${board_name}/uboot-eswin${FEATURE}/
    make ${uboot_config}
    sed -i "s#\(CONFIG_DEFAULT_FDT_FILE=\)\"[^\"]*\"#\1\"eswin/${board_name}.dtb\"#" .config
    make -j$(nproc)
    mkdir -p ${WORK_DIR}/${board_name}/output/
    cp -av u-boot.bin ${WORK_DIR}/${board_name}/output/
    cp -av u-boot.dtb ${WORK_DIR}/${board_name}/output/
    #opensbi
    cd ${WORK_DIR}/${board_name}/opensbi-eswin${FEATURE}
    chiplet=BR2_CHIPLET_1
    chiplet_die=BR2_CHIPLET_1_DIE0_AVAILABLE
    mem_mode=BR2_MEMMODE_FLAT
    if [[ "$board_name" =~ "7702" ]] || [[ "$board_name" =~ "fml13v03" ]];then    
        chiplet=BR2_CHIPLET_2
	chiplet_die=BR2_CHIPLET_1_DIE1_AVAILABLE
    fi
    if [[ "$board_name" =~ "INTER" ]];then
        mem_mode=BR2_MEMMODE_INTERLEAVE
    fi
    make PLATFORM=eswin/eic770x FW_PAYLOAD=y \
     FW_FDT_PATH=${WORK_DIR}/${board_name}/output/u-boot.dtb \
     FW_PAYLOAD_PATH=${WORK_DIR}/${board_name}/output/u-boot.bin \
     CHIPLET="${chiplet}" \
     CHIPLET_DIE_AVAILABLE="${chiplet_die}" \
     MEM_MODE="${mem_mode}" \
     PLATFORM_CLUSTER_X_CORE="BR2_CLUSTER_4_CORE" \
     ENABLE_VPU_SDK=0 \
     ENABLE_ECC=0 \
     -j $(nproc)
    cp -v build/platform/eswin/eic770x/firmware/fw_payload.bin ../output/fw_payload.bin
    if [[ "$board_name" =~ "eic7702-evb" ]];then
        sec_dir=secboot_7702evb
    elif [[ "$board_name" =~ "d560" ]];then
    	sec_dir=secboot_s560
    elif [[ "$board_name" =~ "eic7700-ce1" ]];then
        sec_dir=secboot_ce
    elif [[ "$board_name" =~ "eic7700-sbc" ]];then
        sec_dir=secboot_sbc
    elif [[ "$board_name" =~ "fml13v03" ]];then
        sec_dir=secboot_fml13
    elif [[ "$board_name" =~ "eic7700-z530" ]]||[[ "$board_name" =~ "eic7700-vela" ]];then
        sec_dir=secboot_s260
    elif [[ "$board_name" =~ "p550" ]];then
	sec_dir=secboot_p550
    fi
    if [[ "$board_name" =~ "interleave" ]];then
	rm -rf ${WORK_DIR}/$board_name/inter-eswin
        rsync -au --chmod=u=rwX,go=rX  --exclude .git --exclude .hg --exclude .bzr --exclude CVS $WORK_DIR/source/inter-eswin ${WORK_DIR}/${board_name}/
        sed -i "1s#.*# RISCV_GCC_PREFIX=/opt/riscv-elf/bin/riscv64-unknown-elf-#" ${WORK_DIR}/${board_name}/inter-eswin/Makefile
	sed -i 's/-march=rv64imafd_zifencei//g' ${WORK_DIR}/${board_name}/inter-eswin/Makefile
	sed -i  "s#../build/opensbi#opensbi-eswin${FEATURE}#g" ${WORK_DIR}/${board_name}/inter-eswin/Makefile
        sed -i  "s#../images#output#g" ${WORK_DIR}/${board_name}/inter-eswin/Makefile
	cd ${WORK_DIR}/${board_name}/inter-eswin
	make
	cp -v build/*.bin ${WORK_DIR}/${board_name}/output/
    fi
    #nsign
    cd ${WORK_DIR}/${board_name}/firmware-eswin
    if [[ "$board_name" == "eic7702-evb" ]] || [ "$board_name" == "eic7702-deepcomputing-fml13v03" ] || [ "$board_name" == "eic7702-d560a"  ] || [ "$board_name" == "eic7702-d560" ];then    
        sed -i "s|out=.*|out=${WORK_DIR}/${board_name}/output/bootloader_${board_name}_die0.bin|" bootchain_die0.config
        sed -i "s|out=.*|out=${WORK_DIR}/${board_name}/output/bootloader_${board_name}_die1.bin|" bootchain_die1.config
        secboot_line=`cat -n bootchain_die0.config | grep in= | awk -F " " 'NR==1{print$1}'`
        ddr_line=`cat -n bootchain_die0.config | grep in= | awk -F " " 'NR==2{print$1}'`
        d2d_die0_line=`cat -n bootchain_die0.config | grep in= | awk -F " " 'NR==3{print$1}'`
        d2d_die1_line=`cat -n bootchain_die1.config | grep in= | awk -F " " 'NR==3{print$1}'`
        uboot_line=`cat -n bootchain_die0.config | grep in= | awk -F " " 'NR==4{print$1}'`
        #die0
        sed -i "${secboot_line}s#.*# in=${WORK_DIR}/${board_name}/firmware-eswin/${sec_dir}/die0_sec_fw.bin#" bootchain_die0.config
        sed -i "${ddr_line}s#.*# in=${WORK_DIR}/${board_name}/firmware-eswin/${die0_ddr_name}#" bootchain_die0.config
        sed -i "${d2d_die0_line}s#.*# in=${WORK_DIR}/${board_name}/firmware-eswin/d2d.bin#" bootchain_die0.config
        sed -i "${uboot_line}s#.*# in=${WORK_DIR}/${board_name}/output/fw_payload.bin#" bootchain_die0.config
        #die1
        sed -i "${secboot_line}s#.*# in=${WORK_DIR}/${board_name}/firmware-eswin/${sec_dir}/die1_sec_fw.bin#" bootchain_die1.config
        sed -i "${ddr_line}s#.*# in=${WORK_DIR}/${board_name}/firmware-eswin/${die1_ddr_name}#" bootchain_die1.config
        sed -i "${d2d_die1_line}s#.*# in=${WORK_DIR}/${board_name}/firmware-eswin/die1_d2d_init.bin#" bootchain_die1.config
        ./nsign bootchain_die0.config
        ./nsign bootchain_die1.config
        
        source source.sh die0_autoboot.bin ${WORK_DIR}/${board_name}/output/bootloader_${board_name}_die0.bin recovery_fw_die0.bin
        source source.sh die1_autoboot.bin ${WORK_DIR}/${board_name}/output/bootloader_${board_name}_die1.bin recovery_fw_die1.bin
        cp bootchain.config bootchain_recovery_die0.config
        cp bootchain.config bootchain_recovery_die1.config
        sed -i "5s#.*# in=${WORK_DIR}/${board_name}/firmware-eswin/${sec_dir}/die0_sec_fw.bin#" bootchain_recovery_die0.config
        sed -i "5s#.*# in=${WORK_DIR}/${board_name}/firmware-eswin/${sec_dir}/die1_sec_fw.bin#" bootchain_recovery_die1.config
        sed -i "s/59000000/79000000/g" bootchain_recovery_die1.config
        sed -i "s/80000000/79000000/g" bootchain_recovery_die1.config
        sed -i "32s#.*# in=${WORK_DIR}/${board_name}/firmware-eswin/${die0_recover_ddr_name}#" bootchain_recovery_die0.config
	sed -i "32s#.*# in=${WORK_DIR}/${board_name}/firmware-eswin/${die1_recover_ddr_name}#" bootchain_recovery_die1.config
        sed -i "59s#.*# in=${WORK_DIR}/${board_name}/firmware-eswin/recovery_fw_die0.bin#" bootchain_recovery_die0.config
        sed -i "59s#.*# in=${WORK_DIR}/${board_name}/firmware-eswin/recovery_fw_die1.bin#" bootchain_recovery_die1.config
        sed -i "s|out=.*|out=${WORK_DIR}/${board_name}/output/recovery_bootloader_${board_name}_die0.bin|" bootchain_recovery_die0.config
        sed -i "s|out=.*|out=${WORK_DIR}/${board_name}/output/recovery_bootloader_${board_name}_die1.bin|" bootchain_recovery_die1.config
        ./nsign bootchain_recovery_die0.config
        ./nsign bootchain_recovery_die1.config
    elif [[ "$board_name" =~ "interleave" ]];then
	#interleave
        sed -i "s|out=.*|out=${WORK_DIR}/${board_name}/output/bootloader_${board_name}_inter.bin|" bootchain_interleave.config
        ddr_line=`cat -n bootchain_interleave.config | grep in= | awk -F " " 'NR==1{print$1}'`
        d2d_die0_line=`cat -n bootchain_interleave.config | grep in= | awk -F " " 'NR==2{print$1}'`
        uboot_line=`cat -n bootchain_interleave.config | grep in= | awk -F " " 'NR==3{print$1}'`
        sed -i "${ddr_line}s#.*# in=${WORK_DIR}/${board_name}/firmware-eswin/${die0_ddr_name}#" bootchain_interleave.config
        sed -i "${d2d_die0_line}s#.*# in=${WORK_DIR}/${board_name}/firmware-eswin/d2d.bin#" bootchain_interleave.config
        sed -i "${uboot_line}s#.*# in=${WORK_DIR}/${board_name}/output/load_interleave.bin#" bootchain_interleave.config
        
	    #die1
        sed -i "s|out=.*|out=${WORK_DIR}/${board_name}/output/bootloader_${board_name}_die1.bin|" bootchain_die1.config
        ddr_line=`cat -n bootchain_die1.config | grep in= | awk -F " " 'NR==2{print$1}'`
        d2d_die1_line=`cat -n bootchain_die1.config | grep in= | awk -F " " 'NR==3{print$1}'`
        secboot_line=`cat -n bootchain_die1.config | grep in= | awk -F " " 'NR==1{print$1}'`

        sed -i "${secboot_line}s#.*# in=${WORK_DIR}/${board_name}/firmware-eswin/${sec_dir}/die1_sec_fw.bin#" bootchain_die1.config
        sed -i "${ddr_line}s#.*# in=${WORK_DIR}/${board_name}/firmware-eswin/${die1_ddr_name}#" bootchain_die1.config
        sed -i "${d2d_die1_line}s#.*# in=${WORK_DIR}/${board_name}/firmware-eswin/die1_d2d_init.bin#" bootchain_die1.config
        ./nsign bootchain_interleave.config
        ./nsign bootchain_die1.config

        source source.sh die0_autoboot.bin ${WORK_DIR}/${board_name}/output/bootloader_${board_name}_inter.bin recovery_fw_inter.bin
        source source.sh die1_autoboot.bin ${WORK_DIR}/${board_name}/output/bootloader_${board_name}_die1.bin recovery_fw_die1.bin
        cp bootchain_interleave_recovery.config bootchain_recovery_inter.config
        cp bootchain.config bootchain_recovery_die1.config
        # sed -i "5s#.*# in=${WORK_DIR}/${board_name}/firmware-eswin/die0_sec_fw.bin#" bootchain_recovery_inter.config
        sed -i "5s#.*# in=${WORK_DIR}/${board_name}/firmware-eswin/${sec_dir}/die1_sec_fw.bin#" bootchain_recovery_die1.config
        sed -i "s/59000000/79000000/g" bootchain_recovery_die1.config
        sed -i "s/80000000/79000000/g" bootchain_recovery_die1.config
        # sed -i "32s#.*# in=${WORK_DIR}/${board_name}/firmware-eswin/${ddr_name}#" bootchain_recovery_inter.config
        sed -i "5s#.*# in=${WORK_DIR}/${board_name}/firmware-eswin/${die0_recover_ddr_name}#" bootchain_recovery_inter.config
        sed -i "32s#.*# in=${WORK_DIR}/${board_name}/firmware-eswin/${die1_recover_ddr_name}#" bootchain_recovery_die1.config
        # sed -i "59s#.*# in=${WORK_DIR}/${board_name}/firmware-eswin/recovery_fw_inter.bin#" bootchain_recovery_inter.config
        sed -i "32s#.*# in=${WORK_DIR}/${board_name}/firmware-eswin/recovery_fw_inter.bin#" bootchain_recovery_inter.config
        sed -i "59s#.*# in=${WORK_DIR}/${board_name}/firmware-eswin/recovery_fw_die1.bin#" bootchain_recovery_die1.config
        sed -i "s|out=.*|out=${WORK_DIR}/${board_name}/output/recovery_bootloader_${board_name}_inter.bin|" bootchain_recovery_inter.config
        sed -i "s|out=.*|out=${WORK_DIR}/${board_name}/output/recovery_bootloader_${board_name}_die1.bin|" bootchain_recovery_die1.config
        ./nsign bootchain_recovery_inter.config
        ./nsign bootchain_recovery_die1.config
    else
        sed -i "s|out=.*|out=${WORK_DIR}/${board_name}/output/bootloader_${board_name}.bin|" bootchain_lpcpu.config
        secboot_line=`cat -n bootchain_lpcpu.config | grep in= | awk -F " " 'NR==1{print$1}'`
        ddr_line=`cat -n bootchain_lpcpu.config | grep in= | awk -F " " 'NR==2{print$1}'`
        lpcpu_line=`cat -n bootchain_lpcpu.config | grep in= | awk -F " " 'NR==3{print$1}'`
	uboot_line=`cat -n bootchain_lpcpu.config | grep in= | awk -F " " 'NR==4{print$1}'`
        sed -i "${secboot_line}s#.*# in=${WORK_DIR}/${board_name}/firmware-eswin/${sec_dir}/die0_sec_fw.bin#" bootchain_lpcpu.config
        sed -i "${ddr_line}s#.*# in=${WORK_DIR}/${board_name}/firmware-eswin/${die0_ddr_name}#"  bootchain_lpcpu.config
        sed -i "${lpcpu_line}s#.*# in=${WORK_DIR}/${board_name}/firmware-eswin/die0_lpcpu_load_fw.bin#"  bootchain_lpcpu.config
	sed -i "${uboot_line}s#.*# in=${WORK_DIR}/${board_name}/output/fw_payload.bin#"  bootchain_lpcpu.config
        ./nsign bootchain_lpcpu.config
	source source.sh die0_autoboot.bin ${WORK_DIR}/${board_name}/output/bootloader_${board_name}.bin recovery_fw_die0.bin
        cp -vf bootchain.config bootchain_recover.config
        line_in=`cat -n bootchain_recover.config  | grep in=|awk '{print$1}' |tail -n 1`
        sed -i "${line_in}s|in=.*|in=${WORK_DIR}/${board_name}/firmware-eswin/recovery_fw_die0.bin|" bootchain_recover.config
	secboot_line=`cat -n bootchain_recover.config | grep in= | awk -F " " 'NR==1{print$1}'`
        ddr_line=`cat -n bootchain_recover.config | grep in= | awk -F " " 'NR==2{print$1}'`
	sed -i "${secboot_line}s#.*# in=${WORK_DIR}/${board_name}/firmware-eswin/${sec_dir}/die0_sec_fw.bin#" bootchain_recover.config
        sed -i "${ddr_line}s#.*# in=${WORK_DIR}/${board_name}/firmware-eswin/${die0_recover_ddr_name}#"  bootchain_recover.config
	sed -i "2s|out=.*|out=${WORK_DIR}/${board_name}/output/recovery_bootloader_${board_name}.bin|" bootchain_recover.config
	./nsign bootchain_recover.config
    fi
    cd ${WORK_DIR}/${board_name}/output
    ls -l
}

function make_kernel()
{   
    sudo update-alternatives --set python3 /usr/bin/python3.12 >/dev/null 2>&1 || true
    mkdir -p ${WORK_DIR}/$board_name
    rm -rf ${WORK_DIR}/${board_name}/linux-eswin${FEATURE}
    if [ ! -d $WORK_DIR/source/linux-eswin${FEATURE} ];then
        git clone --depth=1 -b ${RELEASE_TAG}   https://github.com/eswincomputing/linux-stable.git    $WORK_DIR/source/linux-eswin${FEATURE}
    fi
    rsync -au --chmod=u=rwX,go=rX  --exclude .git --exclude .hg --exclude .bzr --exclude CVS $WORK_DIR/source/linux-eswin${FEATURE} ${WORK_DIR}/${board_name}/
    rm -rf ${WORK_DIR}/${board_name}/ukpack/
    rsync -au --chmod=u=rwX,go=rX  --exclude .git --exclude .hg --exclude .bzr --exclude CVS $WORK_DIR/source/ukpack ${WORK_DIR}/${board_name}/
    rm -rf ${WORK_DIR}/${board_name}/*.deb
    rm -rf ${WORK_DIR}/${board_name}/*.changes
    rm -rf ${WORK_DIR}/${board_name}/*.buildinfo
    
    VERSION=`cat ${WORK_DIR}/${board_name}/linux-eswin${FEATURE}/Makefile | head -n 5|grep 'VERSION = ' |cut -d '=' -f 2|xargs`
    PATCHLEVEL=`cat ${WORK_DIR}/${board_name}/linux-eswin${FEATURE}/Makefile | head -n 5|grep 'PATCHLEVEL = ' |cut -d '=' -f 2|xargs`
    SUBLEVEL=`cat ${WORK_DIR}/${board_name}/linux-eswin${FEATURE}/Makefile | head -n 5|grep 'SUBLEVEL = ' |cut -d '=' -f 2|xargs`

    toml_file=${WORK_DIR}/source/ukpack/eswin.toml
    if [[ "$board_name" =~ "7702" ]];then
        toml_file=${WORK_DIR}/source/ukpack/eswin_7702.toml
    fi

    cp -vf $toml_file ${WORK_DIR}/${board_name}/linux-eswin${FEATURE}/eswin.toml
    sed -i s/VERSION/${VERSION}/g ${WORK_DIR}/${board_name}/linux-eswin${FEATURE}/eswin.toml
    sed -i s/PATCHLEVEL/${PATCHLEVEL}/g ${WORK_DIR}/${board_name}/linux-eswin${FEATURE}/eswin.toml
    sed -i s/SUBLEVEL/${SUBLEVEL}/g ${WORK_DIR}/${board_name}/linux-eswin${FEATURE}/eswin.toml
    if [ ! -f ${WORK_DIR}/${board_name}/linux-eswin${FEATURE}/arch/riscv/configs/eic7702_pm_defconfig ];then
        sed -i s/eic7702_pm_defconfig/eic7702_defconfig/g ${WORK_DIR}/${board_name}/linux-eswin${FEATURE}/eswin.toml
    fi
    if [ ! -f ${WORK_DIR}/${board_name}/linux-eswin${FEATURE}/arch/riscv/configs/eic7700_pm_defconfig ];then
        sed -i s/eic7700_pm_defconfig/eic7700_defconfig/g ${WORK_DIR}/${board_name}/linux-eswin${FEATURE}/eswin.toml
    fi

    date_str=`date  +%Y.%m.%d`
    date_str2=`LC_TIME=en_US.UTF-8 date "+%a, %d %b %Y %H:%M:%S"`
    sed -i "s/DATE_STR/${date_str}/g" ${WORK_DIR}/${board_name}/linux-eswin${FEATURE}/eswin.toml
    sed -i "s/STR_DATE/${date_str2}/g" ${WORK_DIR}/${board_name}/linux-eswin${FEATURE}/eswin.toml
    cd ${WORK_DIR}/${board_name}/linux-eswin${FEATURE}
    ${WORK_DIR}/source/ukpack/ukpack -D ./eswin.toml
    #dpkg-buildpackage -a riscv64 -d -b
    dpkg-buildpackage -a riscv64 -t riscv64-unknown-linux-gnu -uc -us --no-sign -d -b
    mkdir -p ${WORK_DIR}/${board_name}/output/
    rm -rf ${WORK_DIR}/${board_name}/output/linux*.deb
    mv -v ../*.deb ${WORK_DIR}/${board_name}/output/
    cd ${WORK_DIR}/${board_name}/output
    ls -l
}
function make_images()
{
    if [ ! -f ${WORK_DIR}/${board_name}/linux-eswin${FEATURE}/eswin.toml  ];then
        make_kernel
    fi
    sudo update-alternatives --set python3 /usr/bin/python3.12 >/dev/null 2>&1 || true
    rm -rf ${WORK_DIR}/${board_name}/risc-v-gadget
    rsync -au --chmod=u=rwX,go=rX  --exclude .git --exclude .hg --exclude .bzr --exclude CVS $WORK_DIR/source/risc-v-gadget ${WORK_DIR}/${board_name}/
    cp -rf ${WORK_DIR}/${board_name}/risc-v-gadget/flash-kernel_${board_name} ${WORK_DIR}/${board_name}/risc-v-gadget/flash-kernel
    kernel_ver=`cat ${WORK_DIR}/${board_name}/linux-eswin${FEATURE}/eswin.toml | grep "version =" | cut -d "=" -f 2|xargs`
    date_str=`grep "${kernel_ver}-" ${WORK_DIR}/${board_name}/linux-eswin${FEATURE}/eswin.toml | grep -oE '[0-9]{4}\.[0-9]{2}\.[0-9]{2}' | head -n 1`
    echo "kernel_ver:$kernel_ver"
    echo "date_str:$date_str"
    export kernel_ver
    export RISCV_PATH=${WORK_DIR}/${board_name}/risc-v-gadget
    cp -vf ${WORK_DIR}/${board_name}/output/*.deb ${WORK_DIR}/${board_name}/risc-v-gadget/deb-packages/
    if [ ! -f $RISCV_PATH/deb-packages/linux-image-eic770*_$kernel_ver-*_riscv64.deb ] ||
           [ ! -f $RISCV_PATH/deb-packages/linux-modules*-eic770*_riscv64.deb ] ||
           [ ! -f $RISCV_PATH/deb-packages/linux-image-$kernel_ver-*_riscv64.deb ] ||
           [ ! -f $RISCV_PATH/deb-packages/linux-headers-$kernel_ver-*_riscv64.deb ];then
            echo " error!!!"
                echo " must to build kernel first!!!"
                return 1
    fi
    sed -i "s/kernel_ver/${kernel_ver}/g" ${WORK_DIR}/${board_name}/risc-v-gadget/image-definition-${board_name}.yaml
    sed -i "s/year/$(date +%Y)/g"         ${WORK_DIR}/${board_name}/risc-v-gadget/image-definition-${board_name}.yaml
    sed -i "s/date_str/${date_str}/g"     ${WORK_DIR}/${board_name}/risc-v-gadget/image-definition-${board_name}.yaml
    RELEASE=$(date +%Y%m%d-%H%M%S)
    sed -i s/RELEASE/${RELEASE}/g ${WORK_DIR}/${board_name}/risc-v-gadget/image-definition-${board_name}.yaml
    if [[ ${board_name} =~ "7702"  ]];then
        sed -i "s/7700/7702/g" ${WORK_DIR}/${board_name}/risc-v-gadget/Makefile
    fi
    cd ${WORK_DIR}/${board_name}
    sudo rm -rf ${WORK_DIR}/${board_name}/ubuntu_output && sudo ${WORK_DIR}/${board_name}/risc-v-gadget/ubuntu-image --workdir ${WORK_DIR}/${board_name}/ubuntu_output --debug classic ${WORK_DIR}/${board_name}/risc-v-gadget/image-definition-${board_name}.yaml | tee output.txt
    sudo mv ${WORK_DIR}/${board_name}/ubuntu_output/*.img ${WORK_DIR}/${board_name}/output/
    sudo chown "$(id -u):$(id -g)" ${WORK_DIR}/${board_name}/output/*.img 2>/dev/null || true
    cd ${WORK_DIR}/${board_name}/output/
    ls -l
}

make_all()
{
    make_bootchain
    make_kernel
    make_images
}

print_comple_method()
{
    echo "you chose $board_name"
    echo "Use the following method to start compiling:"
    echo "    make_bootchain"
    echo "    make_kernel"
    echo "    make_images"
    echo "    make_all:bootchain kernel images"
}



#menu
echo "board list:"
echo [1] eic7700-hifive-premier-p550
echo [2] eic7700-sbc
echo [0] Exit
while true; do
    read -p "please select[0-2]:" -t 5 -n 2 CHOICE
    echo 
    if [[ "$CHOICE" =~ ^[0-9]+$ ]] && (( CHOICE >= 0 && CHOICE <= 2 )); then
        break
    else
        echo "Invalid selection. Please enter a number between 0 and 2!"
    fi
done
echo 
export die0_ddr_name=ddr_fw_die0.bin
export die1_ddr_name=ddr_fw_die1.bin
export die0_recover_ddr_name=ddr_fw_die0_nospi.bin
export die1_recover_ddr_name=ddr_fw_die1_nospi.bin
export FEATURE=
while [ 0 -eq 0 ];
do
    if [ "$CHOICE" == "1" ];then
        export board_name=eic7700-hifive-premier-p550
        export uboot_config=hifive_premier_p550_defconfig
        break
    elif [ "$CHOICE" == "2" ];then
        export board_name=eic7700-sbc
        export uboot_config=eic7700_sbc_defconfig
	export die0_ddr_name=ddr_low_freq/ddr_fw_die0.bin
        export die0_recover_ddr_name=ddr_low_freq/ddr_fw_die0_nospi.bin
        break
    elif [ "$CHOICE" == "0" ];then
        echo "Exiting..."
        break
    else
        echo "Invalid option"
        read -p "please select[0-2]:" -n 1 CHOICE
        echo
    fi
done
print_comple_method
