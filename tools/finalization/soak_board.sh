#!/bin/sh
# 板端正常存储的持续运行测试；默认60秒，可传5..3600秒。
set -eu
duration=${1:-60}
case "$duration" in ''|*[!0-9]*) echo 'Duration must be 5..3600 seconds' >&2; exit 2 ;; esac
[ "$duration" -ge 5 ] && [ "$duration" -le 3600 ] || exit 2
cd /opt/light_monitor
report=$(mktemp -d ./soak.XXXXXX)
echo "SOAK_REPORT=$report"
set +e
(
    sleep "$duration"
    printf 'STATUS\n'
    sleep 1
) | sh ./start_light_monitor.sh -i 100 -l "$report/logs" > "$report/console.log" 2>&1
result=$?
set -e
tail -n 8 "$report/console.log"
echo "app_exit=$result"
wc -l "$report/logs/light_monitor.csv"
echo "SOAK_REPORT=$report"
exit "$result"
