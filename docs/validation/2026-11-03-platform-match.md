# 2026-11-03：最小 platform 驱动匹配及卸载重载实测

本记录保存用户在开发板串口提供的实际输出。实时任务状态以 work_state 为准；本记录证明最小模块首次加载、probe、remove、重载和最终卸载，不证明 GPIO 功能。

## 已验证链路

- Ubuntu 中构建的 lightkey.ko 已复制到 NFS 项目运行目录。
- 独立测试 DTB 为 imx6ull-alientek-emmc-lightkey-test.dtb，用户的 file 输出显示 DTB v17、38797 字节。
- 此前反编译检查已确认测试节点包含 compatible = "lightdemo,key0" 和 status = "okay"。
- 本次板端根目录实际来自约定的 Ubuntu NFS rootfs。
- 运行设备树中存在测试节点，加载模块后实际调用了 probe。
- 后续卸载、再次加载和最终卸载均执行了预期回调，最终 lsmod 筛选无输出。

## 板端原始证据

```text
/ # cat /proc/mounts | grep ' / '
192.168.50.130:/home/jopwang/linux/nfs/rootfs / nfs rw,relatime,vers=3,rsize=4096,wsize=4096,namlen=255,hard,nolock,proto=tcp,timeo=600,retrans=2,sec=sys,mountaddr=192.168.50.130,mountvers=3,mountproto=tcp,local_lock=all,addr=192.168.50.130 0 0
/ # tr '\000' '\n' < /proc/device-tree/lightkey-test/compatible
lightdemo,key0
/ # insmod /opt/light_monitor/lightkey.ko
[   43.379129] lightkey-demo lightkey-test: lightkey probe
```

用户同时提供的内核日志包含：

```text
[    4.406871] input: gpio_keys@0 as /devices/platform/gpio_keys@0/input/input2
[    7.794824] VFS: Mounted root (nfs filesystem) on device 0:14.
[   43.379129] lightkey-demo lightkey-test: lightkey probe
```

## 卸载与重载原始证据

```text
/ # rmmod lightkey
[ 7074.890602] lightkey-demo lightkey-test: lightkey remove
/ # insmod /opt/light_monitor/lightkey.ko
[ 7086.701911] lightkey-demo lightkey-test: lightkey probe
/ # rmmod lightkey
[ 7097.607160] lightkey-demo lightkey-test: lightkey remove
/ # dmesg | grep lightkey
[   43.379129] lightkey-demo lightkey-test: lightkey probe
[ 7074.890602] lightkey-demo lightkey-test: lightkey remove
[ 7086.701911] lightkey-demo lightkey-test: lightkey probe
[ 7097.607160] lightkey-demo lightkey-test: lightkey remove
/ # lsmod | grep lightkey
```

最后一条命令没有输出，用户提供结果时 lightkey 模块已卸载。

## 结论与边界

设备树测试节点、platform 驱动匹配表与 probe/remove 调用的链路已实板验证，任务 01 的最小模块匹配及卸载重载验收通过。驱动当前只打印日志；按键读取、GPIO 申请、中断、字符设备及应用功能尚未验证。

内核已有 gpio_keys@0 输入设备注册。后续对板级 DTS 和底板原理图的核对发现，该节点使用 GPIO1_IO18，而底板 KEY0 接该线；先前将 KEY0 判为 GPIO1_IO01 的推断有误，详见[纠正记录](2026-11-03-key0-mapping.md)。

后续进入任务 02，先核查现有按键驱动、pinctrl 和 GPIO 归属，再实现 GPIO 获取及字符设备读取。
