#!/bin/sh
# 板端：输出设备暂时不可用时的错误报告；不模拟物理引脚卡死。
set -eu
cd /opt/light_monitor
driver=/sys/bus/platform/drivers/lightbeep
client=/sys/bus/platform/devices/lightbeep-test
[ -L "$client/driver" ] || { echo 'Load the buzzer driver first' >&2; exit 1; }
./beep_test off
(
    restore()
    {
        if [ ! -L "$client/driver" ]; then
            echo lightbeep-test > "$driver/bind" || :
        fi
    }
    trap restore 0
    sleep 1
    printf 'SET_THRESHOLD 100 200\n'
    sleep 2
    echo lightbeep-test > "$driver/unbind"
    sleep 2
    printf 'STOP\nSTATUS\n'
    sleep 1
    echo lightbeep-test > "$driver/bind"
    sleep 2
    printf 'STATUS\nSTART\n'
    sleep 1
) | ./light_monitor -i 500 -l ./logs_buzzer_unavailable
# 历史控制失败按应用约定返回1；set -e使脚本也返回1，属预期测试结果。
