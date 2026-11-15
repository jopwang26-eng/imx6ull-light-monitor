#!/bin/sh
# Ubuntu root：只丢弃指定开发板到指定服务端的NFS端口流量12秒。
# 此脚本不停止NFS服务、不清空防火墙，必须等板端RAM观察程序就绪后运行。
set -eu
chain=LM_LIGHT_NFS_TEST
iface=ens37
board=192.168.50.131
server=192.168.50.130
case "${1:-}" in
    --check)
        ip -4 addr show "$iface"
        iptables --version
        command -v systemd-run
        exit 0 ;;
    --run) ;;
    *) echo 'Use --check, or --run READY_HOST_PATH to wait for the RAM observer' >&2; exit 2 ;;
esac
[ "$(id -u)" = 0 ] || { echo 'Run via sudo on Ubuntu' >&2; exit 1; }
ip -4 addr show "$iface" | grep -q '192.168.50.130/24'
if iptables -nL "$chain" >/dev/null 2>&1; then
    echo "Existing $chain: inspect before retry" >&2
    exit 1
fi
ready=$(readlink -m "${2:?fresh NFS_READY host path required}")
case "$ready" in
    /home/jopwang/linux/nfs/rootfs/opt/light_monitor/nfs-observation.*/NFS_READY) ;;
    *) echo 'Unexpected readiness path' >&2; exit 2 ;;
esac
[ ! -e "$ready" ] || { echo 'Stale readiness marker; prepare a new test directory' >&2; exit 1; }
echo "WAITING_FOR_BOARD=$ready timeout=180s"
waited=0
while [ ! -f "$ready" ]; do
    [ "$waited" -lt 180 ] || { echo 'READY_TIMEOUT; no network change' >&2; exit 1; }
    sleep 1
    waited=$((waited + 1))
done
restore=$(mktemp /run/light-monitor-nfs-restore.XXXXXX)
cat > "$restore" <<'RESTORE'
#!/bin/sh
# 固定规则参数，只移除此测试添加的跳转和链。
/sbin/iptables -D INPUT -i ens37 -s 192.168.50.131 -d 192.168.50.130 -j LM_LIGHT_NFS_TEST 2>/dev/null || :
/sbin/iptables -F LM_LIGHT_NFS_TEST 2>/dev/null || :
/sbin/iptables -X LM_LIGHT_NFS_TEST 2>/dev/null || :
if /sbin/iptables -C INPUT -i ens37 -s 192.168.50.131 -d 192.168.50.130 -j LM_LIGHT_NFS_TEST 2>/dev/null; then
    echo 'NFS test jump still installed; restoration needs attention' >&2
    exit 1
fi
RESTORE
chmod 700 "$restore"
unit=light-monitor-nfs-restore-$$
cleanup()
{
    if sh "$restore"; then
        # 确认跳转已移除才取消后备恢复；不改其他单元。
        systemctl stop "$unit.timer" >/dev/null 2>&1 || :
        rm -f "$restore"
    else
        echo "Keep restore timer $unit and $restore for retry" >&2
    fi
}
trap cleanup 0
trap 'exit 1' 1 2 15
# 先登记20秒独立恢复，再添加会影响流量的规则；注册失败则不触碰网络。
systemd-run --unit="$unit" --on-active=20 /bin/sh "$restore"
iptables -N "$chain"
iptables -A "$chain" -p tcp --dport 2049 -j DROP
iptables -A "$chain" -p udp --dport 2049 -j DROP
iptables -I INPUT 1 -i "$iface" -s "$board" -d "$server" -j "$chain"
echo "NFS_DROP_BEGIN $(date -u +%Y-%m-%dT%H:%M:%SZ) board=$board duration=12s"
sleep 12
sh "$restore"
echo "NFS_DROP_END $(date -u +%Y-%m-%dT%H:%M:%SZ)"
