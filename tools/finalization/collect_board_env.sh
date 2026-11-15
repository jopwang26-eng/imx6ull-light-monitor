#!/bin/sh
# 板端只读采集；可在应用退出后运行，输出供最终验收归档。
set -eu
uname -a
cat /proc/uptime
cat /proc/mounts
printf '\nDEVICE_BINDINGS\n'
for device in /sys/bus/platform/devices/lightkey-test /sys/bus/platform/devices/lightbeep-test /sys/bus/i2c/devices/0-001e; do
    printf '%s: ' "$device"
    readlink "$device/driver" || :
done
printf '\nDT_COMPATIBLE\n'
tr '\000' '\n' < /sys/firmware/devicetree/base/compatible
printf '\nAPP_AND_MODULES\n'
cd /opt/light_monitor
if command -v sha256sum >/dev/null 2>&1; then
    sha256sum light_monitor lightkey.ko lightap3216c.ko lightbeep.ko
else
    ls -l light_monitor lightkey.ko lightap3216c.ko lightbeep.ko
fi
