#!/bin/sh
# 在 Ubuntu 普通用户终端运行，sudo认证只在终端交互输入。
set -eu
source_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
runtime=/home/jopwang/linux/nfs/rootfs/opt/light_monitor
binary="$source_dir/light_monitor"
[ -f "$binary" ] || { echo "Build light_monitor first" >&2; exit 1; }
if [ -e "$runtime/light_monitor" ]; then
    sudo cp -a "$runtime/light_monitor" "$runtime/light_monitor.before_$(date +%Y%m%d_%H%M%S)"
fi
sudo install -m 755 "$binary" "$runtime/light_monitor"
if [ -e "$runtime/start_light_monitor.sh" ]; then
    sudo cp -a "$runtime/start_light_monitor.sh" "$runtime/start_light_monitor.before_$(date +%Y%m%d_%H%M%S).sh"
fi
sudo install -m 755 "$source_dir/start_light_monitor.sh" "$runtime/start_light_monitor.sh"
cmp "$binary" "$runtime/light_monitor"
sha256sum "$binary" "$runtime/light_monitor"
echo "DEPLOYED: control-v1"
