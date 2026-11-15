#!/bin/sh
# 板端root：完整运行并恢复正式模块。仅适用于本项目已核对、无额外I2C外设的板。
# 所有输出保存报告；中途错误仍尝试恢复。正式ko始终取/opt/light_monitor原文件。
set -eu
runtime=/opt/light_monitor
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
client=/sys/bus/i2c/devices/0-001e
parameter=/sys/module/lightap_nack_test/parameters/nack_enabled
[ "${1:-}" = --no-extra-i2c ] || { echo 'Confirm no extra I2C devices: use --no-extra-i2c'; exit 2; }
[ ! -e /sys/bus/i2c/devices/0-001f ] || { echo '0x1f already registered; abort'; exit 1; }
[ -d /sys/module/lightap3216c ] || { echo 'Load formal sensor module first'; exit 1; }
[ ! -d /sys/module/lightap_nack_test ] || { echo 'Test module already loaded; inspect first'; exit 1; }
[ "$(basename "$(readlink "$client/driver")")" = lightap3216c ]
command -v pidof >/dev/null 2>&1 || { echo 'pidof required for application precheck'; exit 1; }
if pidof light_monitor >/dev/null 2>&1; then
    echo 'Exit running light_monitor before testing'; exit 1
fi
for node in /dev/lightap3216c /dev/lightbeep /dev/lightkey; do
    [ -c "$node" ] || { echo "Missing $node"; exit 1; }
done
"$runtime/beep_test" off
report=$(mktemp -d "$runtime/i2c-nack-report.XXXXXX")
echo "REPORT_DIR=$report"
sha256sum "$runtime/lightap3216c.ko" > "$report/formal-before.sha256"
changed=0

# 退出时先停故障注入，再卸载测试模块，恢复原正式模块及字符节点。
# 恢复失败返回非零并保留报告，不使用忽略错误来宣称恢复成功。
restore()
{
    result=$?
    trap - 0 1 2 15
    if [ -f "$parameter" ]; then echo 0 > "$parameter" || result=1; fi
    if [ "$changed" = 1 ]; then
        if [ -d /sys/module/lightap_nack_test ]; then
            rmmod lightap_nack_test || result=1
        fi
        if [ ! -d /sys/module/lightap_nack_test ] && [ ! -d /sys/module/lightap3216c ]; then
            insmod "$runtime/lightap3216c.ko" || result=1
        fi
        if [ -d /sys/module/lightap3216c ] && [ -c /dev/lightap3216c ]; then
            "$runtime/lightap_test" -n 3 -i 200 >> "$report/restore.txt" 2>&1 || result=1
            [ "$(basename "$(readlink "$client/driver")")" = lightap3216c ] || result=1
        else
            result=1
        fi
    fi
    sha256sum -c "$report/formal-before.sha256" >> "$report/restore.txt" 2>&1 || result=1
    "$runtime/beep_test" off >> "$report/restore.txt" 2>&1 || result=1
    echo "TEST_EXIT=$result REPORT_DIR=$report"
    cat "$report/direct.txt" "$report/app.txt" "$report/kernel-tail.txt" "$report/restore.txt" 2>/dev/null || :
    exit "$result"
}
trap restore 0
trap 'exit 1' 1 2 15

# rmmod若因仍有读取者而失败，立即结束，不强制卸载或抢占设备。
rmmod lightap3216c
changed=1
insmod "$here/lightap_nack_test.ko"
[ -f "$parameter" ] && [ -c /dev/lightap3216c ]
"$here/check_nack" > "$report/direct.txt" 2>&1

# 同一应用正常→故障→恢复；失败期间STATUS应为NA且报警/输出关闭。
# 生产端完成标记用于防止管道只留下应用退出码而掩盖调度脚本失败。
(
    set -eu
    trap 'echo 0 > "$parameter"' 0
    printf 'SET_THRESHOLD 65534 65535\n'
    sleep 2
    echo 1 > "$parameter"
    sleep 1
    printf 'STATUS\n'
    sleep 1
    echo 0 > "$parameter"
    sleep 2
    printf 'STATUS\n'
    sleep 1
    echo done > "$report/producer.done"
) | "$runtime/light_monitor" -i 200 -l "$report/logs" > "$report/app.txt" 2>&1
[ -f "$report/producer.done" ]
grep -q 'STATUS .*valid=0 ALS_RAW=NA .*last_errno=5 .*alarm=OFF .*buzzer=OFF' "$report/app.txt"
grep -q 'STATUS .*valid=1 ALS_RAW=[0-9]* .*last_errno=0 ' "$report/app.txt"
grep -q 'DIRECT_RESULT=0' "$report/direct.txt"
grep -q 'EXIT .*buzzer=OFF' "$report/app.txt"
dmesg | tail -n 45 > "$report/kernel-tail.txt"
echo 'CHECKS_OK: direct read errno/buffer, application failure visibility, exit; see report for recovery state'
