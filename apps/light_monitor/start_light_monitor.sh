#!/bin/sh
# 板端root运行：沿用已启动的测试DTB，仅加载缺少的驱动，再启动应用。
set -eu
runtime=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$runtime"
if [ ! -d /sys/module/lightkey ]; then
    insmod ./lightkey.ko
fi
if [ ! -d /sys/module/lightbeep ]; then
    sh ./load_lightbeep.sh
fi
owner=
if [ -L /sys/bus/i2c/devices/0-001e/driver ]; then
    owner=$(basename "$(readlink /sys/bus/i2c/devices/0-001e/driver)")
fi
if [ "$owner" != lightap3216c ]; then
    sh ./load_lightap.sh
fi
for node in /dev/lightkey /dev/lightbeep /dev/lightap3216c; do
    [ -c "$node" ] || { echo "Missing character node: $node" >&2; exit 1; }
done
# 先验证输出可关闭；已有其他进程独占蜂鸣器时停止，不抢占。
./beep_test off
exec ./light_monitor "$@"
