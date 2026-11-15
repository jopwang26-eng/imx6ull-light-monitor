#!/bin/sh
# 板端运行：仅临时接管0-001e，不修改设备树或永久启动设置。
set -eu

runtime=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
client=/sys/bus/i2c/devices/0-001e
old_driver=/sys/bus/i2c/drivers/ap3216c
module="$runtime/lightap3216c.ko"

[ -f "$module" ] || { echo "Missing module: $module" >&2; exit 1; }
[ -d "$client" ] || { echo "Missing I2C client: $client" >&2; exit 1; }

# 如果系统提供fuser，先拒绝接管仍被程序打开的字符设备。
for node in /dev/ap3216c /dev/lightap3216c; do
    if [ -c "$node" ] && command -v fuser >/dev/null 2>&1; then
        if fuser "$node" >/dev/null 2>&1; then
            echo "Close programs using $node before loading" >&2
            exit 1
        fi
    fi
done

owner=
if [ -L "$client/driver" ]; then
    owner=$(basename "$(readlink "$client/driver")")
fi
case "$owner" in
    ap3216c|lightap3216c|'') ;;
    *) echo "Unexpected driver: $owner; leaving it unchanged" >&2; exit 1 ;;
esac

# 已有本项目模块时必须重载，避免磁盘更新但内存仍执行旧代码。
if [ -d /sys/module/lightap3216c ]; then
    rmmod lightap3216c
fi

restore_builtin=0
case "$owner" in ap3216c|lightap3216c) restore_builtin=1 ;; esac
new_loaded=0

rollback()
{
    result=$?
    if [ "$result" -ne 0 ]; then
        # insmod成功不等于probe成功；若后续检查失败，撤销本次加载。
        if [ "$new_loaded" -eq 1 ]; then
            rmmod lightap3216c || :
        fi
        if [ "$restore_builtin" -eq 1 ] && [ ! -L "$client/driver" ]; then
            echo "Restoring built-in ap3216c driver" >&2
            echo 0-001e > "$old_driver/bind" || :
        fi
    fi
}
trap rollback 0

if [ "$owner" = ap3216c ]; then
    echo 0-001e > "$old_driver/unbind"
fi
insmod "$module"
new_loaded=1

# 同时检查真实绑定和节点，捕捉初始化失败但insmod返回成功的情况。
[ -L "$client/driver" ]
[ "$(basename "$(readlink "$client/driver")")" = lightap3216c ]
[ -c /dev/lightap3216c ]
trap - 0
echo "READY: /dev/lightap3216c (als-v1)"
