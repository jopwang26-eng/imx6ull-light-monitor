#!/bin/sh
# Ubuntu：独立构建目录，复制正式源码；不覆盖正式源文件和ko，不自动加载。
set -eu
project=/home/jopwang/linux/IMX6ULL/Drivers/Linux_Drivers/light_monitor
kernel=/home/jopwang/linux/IMX6ULL/linux
cross=/usr/local/arm/gcc-linaro-4.9.4-2017.01-x86_64_arm-linux-gnueabihf/bin/arm-linux-gnueabihf-
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cp "$project/lightap3216c.c" "$here/base.c"
sha256sum "$here/base.c"
make -C "$kernel" M="$here" ARCH=arm CROSS_COMPILE="$cross" modules
"${cross}gcc" -std=gnu99 -Wall -Wextra -Werror -O2 -static "$here/check_nack.c" -o "$here/check_nack"
sha256sum "$here/lightap_nack_test.ko" "$here/check_nack"
