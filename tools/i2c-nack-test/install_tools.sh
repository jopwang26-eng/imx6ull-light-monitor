#!/bin/sh
# Ubuntu交互sudo执行：仅安装独立测试目录，不覆盖正式运行文件。
set -eu
[ "$(id -u)" = 0 ] || { echo 'Run via sudo on Ubuntu'; exit 1; }
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
target=/home/jopwang/linux/nfs/rootfs/opt/light_monitor/i2c-nack-test
[ ! -e "$target" ] || { echo 'Target already exists; inspect before replacing'; exit 1; }
cd "$here"
printf '%s\n' \
    'b326f2cc344c10f16f30c7ecbce670d57c66697b07764199e9962a6ddea0f45c  lightap_nack_test.ko' \
    '18f856ab344e556f322b6fa596f86a3940f4003898aafa8fb21e827f431e41de  check_nack' | sha256sum -c -
install -d -m 755 "$target"
install -m 644 lightap_nack_test.ko base.c lightap_nack_test.c check_nack.c Makefile README.md "$target/"
install -m 755 check_nack run_board.sh "$target/"
echo "INSTALLED=$target"
