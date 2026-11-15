# External modules for the target vendor BSP. Invoke through kernel Kbuild:
# make -C "$KERNEL_DIR" M="$PWD" ARCH=arm CROSS_COMPILE="$CROSS_COMPILE" modules
obj-m += drivers/lightkey/
obj-m += drivers/lightap3216c/
obj-m += drivers/lightbeep/
