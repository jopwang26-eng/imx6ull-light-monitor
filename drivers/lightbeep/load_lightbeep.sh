#!/bin/sh
# 板端运行，必须已用独立 lightbeep 测试 DTB 启动。
set -eu
runtime=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
node=/sys/firmware/devicetree/base/lightbeep-test
[ -d "$node" ] || { echo "Boot lightbeep-test DTB first" >&2; exit 1; }
[ ! -e /sys/class/leds/beep ] || { echo "Original beep still registered" >&2; exit 1; }
if [ -d /sys/module/lightbeep ]; then
    rmmod lightbeep
fi
insmod "$runtime/lightbeep.ko"
# insmod 成功不保证 probe 成功；检查设备节点、绑定和默认逻辑输出。
if [ ! -c /dev/lightbeep ] ||
   [ "$(basename "$(readlink /sys/bus/platform/devices/lightbeep-test/driver)")" != lightbeep ] ||
   ! "$runtime/beep_test" status; then
    echo "Probe or readback failed; unloading" >&2
    rmmod lightbeep || :
    exit 1
fi
echo "READY: /dev/lightbeep (beep-v1.1)"
