#!/bin/sh
# Ubuntu：sudo sh install_release.sh RELEASE_DIR；只安装清单中的运行文件。
set -eu
[ "$(id -u)" = 0 ] || { echo 'Run on Ubuntu via sudo' >&2; exit 1; }
release=$(CDPATH= cd -- "${1:?release directory required}" && pwd)
runtime=/home/jopwang/linux/nfs/rootfs/opt/light_monitor
dtb=imx6ull-alientek-emmc-lightbeep-test.dtb
(cd "$release" && sha256sum -c MANIFEST.sha256 >/dev/null)
backup="$runtime/backup_release_$(date +%Y%m%d_%H%M%S)"
mkdir -p "$backup"
for name in lightkey.ko lightap3216c.ko lightbeep.ko light_monitor beep_test beep_api_test lightap_test nfs_observer load_lightap.sh load_lightbeep.sh restore_ap3216c.sh start_light_monitor.sh collect_board_env.sh soak_board.sh test_buzzer_unavailable.sh prepare_nfs_board.sh; do
    if [ -e "$runtime/$name" ]; then cp -a "$runtime/$name" "$backup/"; fi
    case "$name" in *.ko) mode=644 ;; *) mode=755 ;; esac
    install -m "$mode" "$release/bin/$name" "$runtime/$name"
    cmp "$release/bin/$name" "$runtime/$name"
done
if [ -e "/var/lib/tftpboot/$dtb" ]; then cp -a "/var/lib/tftpboot/$dtb" "$backup/"; fi
install -m 644 "$release/bin/$dtb" /var/lib/tftpboot/
cmp "$release/bin/$dtb" "/var/lib/tftpboot/$dtb"
echo "INSTALLED_RELEASE=$release BACKUP=$backup"
