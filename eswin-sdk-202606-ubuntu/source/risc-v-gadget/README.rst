RISC-V gadget
=============

Build image
-----------

.. code-block:: bash

    sudo ubuntu-image --debug classic image-definition.yaml

For debugging add --workdir /tmp/workdir.

The produced image file ubuntu-<release>-preinstalled-server-riscv64.img.

Burning
----------

Copy the bootloader and Ubuntu image to an ext4-formatted USB disk, connect it to the development board, and enter the uboot command line mode through the serial port.

1, Bootloader burning

.. code-block:: bash

    ext4load usb 0 0x90000000 bootloader.bin
    es_burn write 0x90000000 flash

2, Ubuntu burning

.. code-block:: bash

    es_fs write usb 0 ubuntu.img mmc 0
    reset

First boot
----------

Login with user ubuntu, password ubuntu.

Image
----------

bootloader

.. code-block:: bash

    bootloader/bootloader_ddr5_secboot_P550.bin

ubuntu

.. code-block:: bash

    https://github.com/guopf307/risc-v-gadget/actions/runs/14897367581
