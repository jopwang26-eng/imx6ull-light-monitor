# 部署指南：从公开仓库到开发板

适用对象：i.MX6ULL开发板、已经能工作的厂商Linux 4.1.15 BSP、Ubuntu交叉构建主机，以及TFTP加载内核/DTB、NFS挂载根文件系统的实验环境。本指南使用仓库当前分目录布局，不要求本地历史交付压缩包。

下面是部署文档及命令的静态核对结果，不代表在新电脑或新板卡上重新完成了部署。既有实板结果见[验证汇总](VALIDATION.md)。

## 1. 先确认环境与操作位置

| 位置 | 执行内容 |
|---|---|
| Windows | 编辑文件、Tera Term串口；不能在这里直接运行ARM模块构建命令 |
| Ubuntu终端 | 克隆、交叉编译、备份、安装NFS运行文件及TFTP测试DTB；需要权限时交互sudo |
| U-Boot串口，提示符通常为`=>` | 临时设置网络/bootargs，TFTP下载，bootz启动 |
| Linux板端串口，提示符通常为`#` | 加载驱动、运行应用、检查设备和退出码；BusyBox环境不使用sudo |

外部前提：

- 厂商内核已经配置、构建并具有匹配的`.config`、`Module.symvers`和生成头文件；工具链为ARM hard-float，本项目原验收使用Linaro GCC 4.9.4。
- 已有匹配的zImage、可用ARM根文件系统，以及TFTP/NFS服务；先用原厂内核/原DTB独立启动基线。公开仓库不提供整个BSP、工具链、zImage或rootfs，也不替代这些组件的安装说明。
- 串口115200、8N1、无硬件流控；连接实验网口。KEY0占用GPIO1_IO18，不能把它当UART硬件CTS使用。
- 原板级DTS存在`gpio_keys`、`pinctrl_beep`、`/leds`及原AP3216C配置，且资源与[硬件说明](HARDWARE.md)一致。其他板卡须先适配，不能直接照搬测试DTS。

Ubuntu克隆仓库后进入根目录。以下变量在同一个Ubuntu终端设置，`/path/to/...`必须改成实际绝对路径：

```sh
git clone https://github.com/jopwang26-eng/imx6ull-light-monitor.git
cd imx6ull-light-monitor
export PROJECT_DIR="$(pwd)"
export KERNEL_DIR=/path/to/vendor-linux
export CROSS_COMPILE=/path/to/toolchain/bin/arm-linux-gnueabihf-
export NFS_ROOT=/path/to/exported/rootfs
export TFTP_ROOT=/var/lib/tftpboot
export TEST_DTB=imx6ull-alientek-emmc-lightbeep-test.dtb
```

`NFS_ROOT`指目标rootfs本身，包含bin、lib等目录，不是其上一级导出目录；`TFTP_ROOT`是TFTP实际服务目录。Ubuntu路径与板端路径对应如下：

```text
Ubuntu: $NFS_ROOT/opt/light_monitor
板端:   /opt/light_monitor
Ubuntu: $TFTP_ROOT/$TEST_DTB
U-Boot: tftp下载文件名 imx6ull-alientek-emmc-lightbeep-test.dtb
```

Ubuntu只读预检：

```sh
test -d "$KERNEL_DIR/arch/arm/boot/dts"
test -f "$KERNEL_DIR/Module.symvers"
test -f "$KERNEL_DIR/include/config/kernel.release"
test -d "$NFS_ROOT/bin"
test -d "$TFTP_ROOT"
"${CROSS_COMPILE}gcc" --version
cat "$KERNEL_DIR/include/config/kernel.release"
ip -4 addr
sudo exportfs -v
ls -l "$TFTP_ROOT/zImage"
```

核对NFS导出权限/路径覆盖此rootfs，服务允许实验网段访问，zImage和板端rootfs匹配。不要为了本项目关闭全局防火墙或改动其他服务。连接主机所用的SSH地址与开发板访问NFS/TFTP的网卡地址可以不同。

## 2. 构建运行产物

Ubuntu，在仓库根目录执行：

```sh
cd "$PROJECT_DIR"
make -C "$KERNEL_DIR" M="$PROJECT_DIR" ARCH=arm CROSS_COMPILE="$CROSS_COMPILE" modules
make -B -C apps/light_monitor CROSS_COMPILE="$CROSS_COMPILE" LDFLAGS=-static
"${CROSS_COMPILE}gcc" -std=gnu99 -Wall -Wextra -Werror -O2 -static drivers/lightbeep/beep_test.c -o drivers/lightbeep/beep_test
```

日常启动必需文件共9个：

| 仓库中的产物/脚本 | 安装到板端`/opt/light_monitor/` |
|---|---|
| drivers/lightkey/lightkey.ko | lightkey.ko |
| drivers/lightap3216c/lightap3216c.ko | lightap3216c.ko |
| drivers/lightbeep/lightbeep.ko | lightbeep.ko |
| apps/light_monitor/light_monitor | light_monitor |
| drivers/lightbeep/beep_test | beep_test |
| drivers/lightap3216c/load_lightap.sh | load_lightap.sh |
| drivers/lightap3216c/restore_ap3216c.sh | restore_ap3216c.sh |
| drivers/lightbeep/load_lightbeep.sh | load_lightbeep.sh |
| apps/light_monitor/start_light_monitor.sh | start_light_monitor.sh |

其中beep_test是启动脚本确认蜂鸣器可关闭的依赖，不能遗漏。三个模块由顶层Kbuild递归生成在各自驱动目录，不在仓库根目录。

Ubuntu检查产物：

```sh
file apps/light_monitor/light_monitor drivers/lightbeep/beep_test drivers/lightkey/lightkey.ko
/sbin/modinfo -F vermagic drivers/lightkey/lightkey.ko
/sbin/modinfo -F vermagic drivers/lightap3216c/lightap3216c.ko
/sbin/modinfo -F vermagic drivers/lightbeep/lightbeep.ko
```

用户态应为ARM且静态链接，模块应为ARM relocatable。vermagic需与实际板端`uname -r`及SMP/PREEMPT/MODVERSIONS等配置匹配；只看到“4.1.15”不能代替完整匹配。

可选：KEY0专项测试、传感器读取工具和蜂鸣器API测试的构建命令见[BUILD.md](BUILD.md)。它们不是正常启动必需文件。

## 3. 构建独立测试DTB

Ubuntu：先备份BSP中已有同名测试DTS，再复制这两个测试文件；不覆盖厂商基础板级DTS：

```sh
cd "$PROJECT_DIR"
DTS_BACKUP="$(mktemp -d /tmp/light-monitor-dts-backup.XXXXXX)"
for name in imx6ull-alientek-emmc-lightkey-test.dts imx6ull-alientek-emmc-lightbeep-test.dts; do
    if [ -f "$KERNEL_DIR/arch/arm/boot/dts/$name" ]; then
        cp -p "$KERNEL_DIR/arch/arm/boot/dts/$name" "$DTS_BACKUP/"
    fi
    cp "device-tree/$name" "$KERNEL_DIR/arch/arm/boot/dts/$name"
done
printf 'DTS_BACKUP=%s\n' "$DTS_BACKUP"
make -C "$KERNEL_DIR" ARCH=arm CROSS_COMPILE="$CROSS_COMPILE" "$TEST_DTB"
test -s "$KERNEL_DIR/arch/arm/boot/dts/$TEST_DTB"
```

此DTS备份在/tmp中，需长期保留时自行复制到持久目录。两个DTS的include链为：蜂鸣器测试DTS→KEY0测试DTS→厂商`imx6ull-alientek-emmc.dts`。若厂商BSP缺少自定义DTB目标，先按该BSP规则添加该DTB构建项，不能换成不匹配的主线设备树。

测试DTS释放原gpio-keys的KEY0和原leds的蜂鸣器资源；蜂鸣器启用SION以支持输出回读。内核应继续保留现有AP3216C I²C client，随后由加载脚本临时从内置驱动接管。

## 4. 在Ubuntu准备部署目录、备份并安装

更新已有板端程序前，先在板端前台应用中Ctrl+C，等待回到Shell并立即读取退出码；同时关闭其他占用KEY0、传感器和蜂鸣器的测试进程。不要在正在运行的NFS可执行文件上覆盖应用。磁盘文件替换不等于已加载模块替换，安装完成后按第5节重启测试内核/DTB。

### 4.1 生成临时部署目录

Ubuntu：以下区块启用`set -eu`，前一步失败应停止，不继续复制未完成的文件：

```sh
set -eu
cd "$PROJECT_DIR"
DEPLOY_DIR="$(mktemp -d /tmp/light-monitor-deploy.XXXXXX)"
mkdir "$DEPLOY_DIR/runtime"
install -m 644 drivers/lightkey/lightkey.ko drivers/lightap3216c/lightap3216c.ko drivers/lightbeep/lightbeep.ko "$DEPLOY_DIR/runtime/"
install -m 755 apps/light_monitor/light_monitor drivers/lightbeep/beep_test "$DEPLOY_DIR/runtime/"
install -m 755 drivers/lightap3216c/load_lightap.sh drivers/lightap3216c/restore_ap3216c.sh drivers/lightbeep/load_lightbeep.sh apps/light_monitor/start_light_monitor.sh "$DEPLOY_DIR/runtime/"
install -m 644 "$KERNEL_DIR/arch/arm/boot/dts/$TEST_DTB" "$DEPLOY_DIR/"
(
    cd "$DEPLOY_DIR"
    find . -type f ! -name MANIFEST.sha256 -print | LC_ALL=C sort | xargs sha256sum > MANIFEST.sha256
    sha256sum -c MANIFEST.sha256
)
printf 'DEPLOY_DIR=%s\n' "$DEPLOY_DIR"
```

这里部署9个运行文件和1个独立测试DTB，不安装故障注入模块，也不执行网络中断脚本。

### 4.2 备份并安装

Ubuntu，同一个终端；先打印并检查目标绝对路径，再执行备份和安装：

```sh
NFS_ROOT="$(readlink -f "$NFS_ROOT")"
TFTP_ROOT="$(readlink -f "$TFTP_ROOT")"
test -n "$NFS_ROOT" && test "$NFS_ROOT" != /
test -d "$NFS_ROOT/bin" && test -d "$TFTP_ROOT"
RUNTIME="$NFS_ROOT/opt/light_monitor"
printf 'RUNTIME=%s\nDTB_TARGET=%s/%s\n' "$RUNTIME" "$TFTP_ROOT" "$TEST_DTB"
BACKUP_BASE="$HOME/light-monitor-backups"
mkdir -p "$BACKUP_BASE"
BACKUP_DIR="$(mktemp -d "$BACKUP_BASE/backup.XXXXXX")"
if [ -e "$RUNTIME" ]; then
    test -d "$RUNTIME" && test ! -L "$RUNTIME"
    sudo cp -a "$RUNTIME" "$BACKUP_DIR/runtime"
fi
if [ -e "$TFTP_ROOT/$TEST_DTB" ]; then
    sudo cp -a "$TFTP_ROOT/$TEST_DTB" "$BACKUP_DIR/$TEST_DTB"
fi
printf 'BACKUP_DIR=%s\n' "$BACKUP_DIR"
(cd "$DEPLOY_DIR" && sha256sum -c MANIFEST.sha256)
sudo install -d -m 755 "$RUNTIME"
for source in "$DEPLOY_DIR/runtime/"*; do
    name="$(basename "$source")"
    case "$name" in *.ko) mode=644 ;; *) mode=755 ;; esac
    sudo install -m "$mode" "$source" "$RUNTIME/$name"
    sudo cmp "$source" "$RUNTIME/$name"
done
sudo install -m 644 "$DEPLOY_DIR/$TEST_DTB" "$TFTP_ROOT/$TEST_DTB"
sudo cmp "$DEPLOY_DIR/$TEST_DTB" "$TFTP_ROOT/$TEST_DTB"
printf 'INSTALLED_RUNTIME=%s\nBACKUP_DIR=%s\n' "$RUNTIME" "$BACKUP_DIR"
```

把BACKUP_DIR记到自己的部署记录，备份包括已有日志与同名测试DTB；sudo密码只在Ubuntu交互输入。此流程没有替换zImage、原板级DTB或永久启动环境。已有运行目录为符号链接时流程拒绝继续，先检查真实目标。

本流程逐文件安装，不是原子事务；任一步失败停止启动新版本，按第7节恢复。首次部署原来没有runtime/测试DTB备份是正常情况，需要回到原厂基线启动方式，而不是假设存在旧应用。

## 5. 从U-Boot临时启动

可用原串口会话运行[Tera Term模板](../tools/boot_lightbeep.ttl)，但必须先把其中的实验网卡IP、MAC和NFS路径改为自己的环境；或者在U-Boot按下面步骤逐条输入。

以下为独立实验网示例：服务端192.168.50.130、开发板192.168.50.131。`/home/yourname/linux/nfs/rootfs`必须替换成第1节实际NFS_ROOT路径，不可照抄占位用户名。U-Boot不会继承Ubuntu终端的变量。

```text
printenv bootcmd bootargs ipaddr serverip ethaddr
setenv ipaddr 192.168.50.131
setenv serverip 192.168.50.130
setenv netmask 255.255.255.0
ping 192.168.50.130
tftp 80800000 zImage
tftp 83000000 imx6ull-alientek-emmc-lightbeep-test.dtb
setenv bootargs 'console=ttymxc0,115200 root=/dev/nfs rw nfsroot=192.168.50.130:/home/yourname/linux/nfs/rootfs,vers=3,tcp ip=192.168.50.131:192.168.50.130::255.255.255.0::eth0:off'
bootz 80800000 - 83000000
```

在原实验板上这两个加载地址用于现有内核/DTB；其他内存布局需先按BSP确认。保留板子的有效MAC，若ethaddr未配置则按实验网络分配唯一MAC，不将模板MAC复用于多块板。每次TFTP必须看到成功传输后才进行下一步。

只改本次会话，不执行saveenv、不改永久bootcmd。先保存printenv原始输出，以便恢复。故障时从原启动流程加载原DTB，不让测试DTB覆盖唯一可启动文件。

## 6. 板端检查、启动与部署验收

Linux板端Shell：先核对内核、根挂载和运行设备树：

```sh
uname -r
cat /proc/mounts
ls -ld /sys/firmware/devicetree/base/lightkey-test
ls -ld /sys/firmware/devicetree/base/lightbeep-test
ls -l /sys/bus/i2c/devices/0-001e
cd /opt/light_monitor
ls -l lightkey.ko lightap3216c.ko lightbeep.ko light_monitor beep_test start_light_monitor.sh load_lightap.sh load_lightbeep.sh restore_ap3216c.sh
sh ./start_light_monitor.sh -l ./logs_demo
```

启动脚本按顺序加载KEY0、蜂鸣器，并在必要时从BSP原驱动临时接管AP3216C；检查三字符节点后调用beep_test off，再exec应用。已经装入的正常模块不会被这个入口强制重载，因此更新部署后需要重启，或在确认没有使用者时明确卸载/重载。

看到`READY control-v1`和`SAMPLE`后，在应用前台逐条输入并回车：

```text
STATUS
STOP
STATUS
START
SET_INTERVAL 500
STATUS
```

STOP后应为PAUSED、无新的采样、读数NA；START恢复；周期设置后显示interval_ms=500。按一次KEY0应在RUNNING/PAUSED间切换。若屏幕为Shell的`#`，不要输入START等应用命令，先启动应用。

需要检查报警时，根据当时原始值选择LOW/HIGH后输入`SET_THRESHOLD LOW HIGH`，验证低于LOW开启、高于HIGH解除；中间及等号保持，暂停/故障/退出尝试关闭。未设置阈值时默认DISABLED，不能以默认无声判断报警坏了。原始值不是校准lux。

最后Ctrl+C，等回到Shell后立即记录：

```sh
echo "app_exit=$?"
ls -l /dev/lightkey /dev/lightbeep /dev/lightap3216c
readlink /sys/bus/platform/devices/lightkey-test/driver
readlink /sys/bus/platform/devices/lightbeep-test/driver
readlink /sys/bus/i2c/devices/0-001e/driver
./beep_test status
head -n 3 ./logs_demo/light_monitor.csv
tail -n 3 ./logs_demo/light_monitor.csv
dmesg | tail -n 30
```

绑定应分别为lightkey-demo、lightbeep、lightap3216c；退出输出应确认蜂鸣器OFF。正常无历史错误的这次运行通常应退出0，出现1时结合EXIT各项统计判断，不能被后续echo覆盖原退出码。CSV含sequence、elapsed_ms、als_raw、alarm且只记录成功采样。

检查单独设备前必须先退出应用，因为KEY0和蜂鸣器采用独占打开。可选验收工具若已按BUILD编译，可从各自驱动目录复制到runtime；`collect_board_env.sh`、`soak_board.sh`也可单独复制，但不是正常部署前提。部署完成只需要正常链路验收，不自动重复NFS中断或NACK测试。

## 7. 失败后的恢复

若新安装的用户态工具已经无法执行，不依赖它完成恢复：先在Ubuntu按下面的备份流程恢复文件，再通过原启动流程重启；保留串口错误输出。以下板端命令适用于Shell和相关工具仍可正常执行的情况。

先在板端退出应用和其他读取者。在仍可用的板端Shell执行，某步失败就先处理，不强制卸载：

```sh
cd /opt/light_monitor
./beep_test off
sh ./restore_ap3216c.sh
if [ -d /sys/module/lightbeep ]; then rmmod lightbeep; fi
if [ -d /sys/module/lightkey ]; then rmmod lightkey; fi
```

restore脚本要求BSP原内置ap3216c驱动存在；如果当前系统没有该驱动，不能宣称原绑定已恢复。应用退出后才能释放独占fd，否则rmmod返回busy应停止处理。

Ubuntu：使用第4节打印的实际BACKUP_DIR；若另开终端，重新设置NFS_ROOT、TFTP_ROOT、TEST_DTB、RUNTIME和BACKUP_DIR。确认这些绝对路径对应本次实验后，把失败版本另存，再恢复原目录：

```sh
test -d "$BACKUP_DIR"
test "$RUNTIME" = "$NFS_ROOT/opt/light_monitor"
FAILED_DIR="$(sudo mktemp -d "$NFS_ROOT/opt/light-monitor-failed.XXXXXX")"
if [ -d "$RUNTIME" ]; then
    sudo mv "$RUNTIME" "$FAILED_DIR/runtime"
fi
if [ -d "$BACKUP_DIR/runtime" ]; then
    sudo cp -a "$BACKUP_DIR/runtime" "$RUNTIME"
else
    printf 'First deployment: no previous runtime; failed files retained at %s\n' "$FAILED_DIR"
fi
if [ -f "$BACKUP_DIR/$TEST_DTB" ]; then
    sudo cp -a "$BACKUP_DIR/$TEST_DTB" "$TFTP_ROOT/$TEST_DTB"
fi
```

没有旧同名测试DTB时，不把新测试文件当成原厂DTB；保持它独立命名，并在U-Boot改回已记录的原DTB/原bootargs或原始启动流程。重新启动后再检查模块和绑定。恢复运行目录会保留原备份中的日志，新部署期间的数据仍在FAILED_DIR，不直接删除。

## 8. 常见问题与检查清单

| 现象 | 首先检查 |
|---|---|
| TFTP超时或File not found | 服务网卡、实验IP、TFTP根目录、文件名、读取权限及服务端口 |
| NFS根无法挂载 | bootargs完整路径、导出范围、NFSv3/TCP支持与网络；先恢复原厂可启动基线 |
| Invalid module format/Unknown symbol | 内核版本、配置、Module.symvers、工具链和vermagic；不强制insmod |
| insmod后没有字符节点 | 实际DTB节点、driver绑定和probe错误；insmod成功不等于probe成功 |
| Missing beep_test/设备节点 | 第2节九文件清单、运行目录和启动脚本顺序 |
| 原beep仍注册/GPIO busy | 是否运行本测试DTB、旧gpio-leds/gpio-keys是否释放资源 |
| 蜂鸣器初始化EIO | 测试DTB的SION配置及GPIO逻辑回读，不删除回读校验掩盖错误 |
| EBUSY/rmmod失败 | 应用或专项测试是否仍持有设备；不强制卸载 |
| shell提示START not found | 应用尚未运行，命令被输到了板端Shell |
| CSV或退出等待较久 | rootfs/日志是否在hard NFS、在途写入是否阻塞；不承诺立即退出 |

部署文档核对清单：

- 环境前提、操作终端及NFS/TFTP路径对应关系明确。
- 三模块、应用、启动必需beep_test及四脚本均有来源与安装命令。
- 独立DTB include链、构建、备份、安装与临时启动参数明确。
- 安装前停止运行者，核对架构/vermagic，复制后校验内容。
- 启动、绑定、按键/命令、CSV、退出关闭有正常验收步骤。
- 首次部署、已有部署及部分安装失败均有恢复边界。

`tools/finalization/build_release.sh`、`install_release.sh`以及驱动目录的deploy脚本保留原实验的平铺路径和本地交付约定，不能直接替代上述公开仓库部署流程。尤其历史发布脚本还依赖不在Git中的已板测NACK二进制附件；从新克隆的仓库日常部署请使用本指南，不要求自行寻找未公开压缩包。
