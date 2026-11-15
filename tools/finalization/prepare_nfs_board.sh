#!/bin/sh
# 板端root：准备专用RAM挂载，先完成全部NFS读取，再启动静态观察程序。
set -eu
cd /opt/light_monitor
./beep_test off
for node in /dev/lightkey /dev/lightbeep /dev/lightap3216c; do
    [ -c "$node" ] || { echo "Missing $node; run start_light_monitor.sh then exit first" >&2; exit 1; }
done
ram=$(mktemp -d /tmp/light-monitor-ram.XXXXXX)
mount -t tmpfs -o size=16m tmpfs "$ram"
cp ./light_monitor ./nfs_observer "$ram/"
chmod 755 "$ram/light_monitor" "$ram/nfs_observer"
logs=$(mktemp -d ./nfs-observation.XXXXXX)
logs="/opt/light_monitor/${logs#./}"
echo "RAM_DIR=$ram"
echo "NFS_LOG_DIR=$logs"
echo "NFS_READY_HOST_PATH=/home/jopwang/linux/nfs/rootfs$logs/NFS_READY"
echo "Prepared only; no network change. Start observer with:"
echo "LM_NFS_READY_FILE=$logs/NFS_READY $ram/nfs_observer $ram/light_monitor $logs 40 $ram/report.txt"
# 保留RAM目录和报告，不自动卸载；确认观察程序结束后再由用户查看。
