#!/bin/sh
# 关闭采集程序后运行，恢复BSP自带的传感器驱动。
set -eu
client=/sys/bus/i2c/devices/0-001e

if [ -d /sys/module/lightap3216c ]; then
    # 文件仍打开时普通rmmod失败，脚本会停止，不强制卸载。
    rmmod lightap3216c
fi
if [ -L "$client/driver" ]; then
    owner=$(basename "$(readlink "$client/driver")")
    [ "$owner" = ap3216c ] || { echo "Unexpected driver: $owner" >&2; exit 1; }
else
    echo 0-001e > /sys/bus/i2c/drivers/ap3216c/bind
fi
[ -c /dev/ap3216c ]
echo "RESTORED: /dev/ap3216c"
