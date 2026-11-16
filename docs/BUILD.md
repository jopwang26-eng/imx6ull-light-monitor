# 构建与运行

完整的文件安装、备份、NFS/TFTP对应关系、U-Boot启动、板端验收及回滚步骤见[部署指南](DEPLOY.md)。本页侧重构建命令和原生测试。

## 环境与目录

已验证环境为 i.MX6ULL、厂商 Linux 4.1.15 BSP、Linaro GCC 4.9.4 ARM hard-float工具链。仓库不包含完整BSP、工具链、zImage或NFS根文件系统；主机内核版本不是目标板模块版本。

公开仓库采用按模块分目录的布局。部分历史脚本沿用原实验环境的平铺工程路径，不能直接把这些路径视为新电脑的实际配置；先查看脚本并设置为自己的环境，历史构建日志只作为验收证据。

## ARM模块及应用

在Linux主机的仓库根目录执行，替换示例路径：

```sh
export KERNEL_DIR=/path/to/vendor-linux
export CROSS_COMPILE=/path/to/toolchain/bin/arm-linux-gnueabihf-
make -C "$KERNEL_DIR" M="$PWD" ARCH=arm CROSS_COMPILE="$CROSS_COMPILE" modules
make -C apps/light_monitor CROSS_COMPILE="$CROSS_COMPILE" LDFLAGS=-static
"${CROSS_COMPILE}gcc" -std=gnu99 -Wall -Wextra -Werror -O2 -static drivers/lightkey/key_event_test.c -o drivers/lightkey/key_event_test
"${CROSS_COMPILE}gcc" -std=gnu99 -Wall -Wextra -Werror -O2 -static drivers/lightap3216c/lightap_test.c -o drivers/lightap3216c/lightap_test
"${CROSS_COMPILE}gcc" -std=gnu99 -Wall -Wextra -Werror -O2 -static drivers/lightbeep/beep_test.c -o drivers/lightbeep/beep_test
"${CROSS_COMPILE}gcc" -std=gnu99 -Wall -Wextra -Werror -O2 -static drivers/lightbeep/beep_api_test.c -o drivers/lightbeep/beep_api_test
```

模块输出分别位于三个 `drivers/<模块>/` 目录；加载前核对vermagic与板端内核一致。顶层Makefile仅组织外部模块，不构建或部署整个BSP。

## 设备树

将 `device-tree/` 下两个测试DTS复制到BSP的 `arch/arm/boot/dts/`，保留厂商原板级DTS和include链。构建：

```sh
make -C "$KERNEL_DIR" ARCH=arm CROSS_COMPILE="$CROSS_COMPILE" imx6ull-alientek-emmc-lightbeep-test.dtb
```

蜂鸣器测试DTS继承KEY0测试DTS，后者包含厂商 `imx6ull-alientek-emmc.dts`。原厂基础DTS及原理图PDF没有复制到公开仓库。资源释放及引脚配置见[硬件说明](HARDWARE.md)。

## 原生应用回归

需要Linux、gcc与Python 3，不连接板端设备：

```sh
cd apps/light_monitor
make native
gcc -Wall -Wextra -Werror -shared -fPIC tests/fsync_gate.c -ldl -o fsync_gate.so
gcc -Wall -Wextra -Werror -shared -fPIC tests/control_faults.c -ldl -o control_faults.so
python3 tests/test_process.py ./light_monitor_native
python3 tests/test_logging.py ./light_monitor_native
python3 tests/test_control.py ./light_monitor_native
gcc -std=gnu99 -Wall -Wextra -Werror -O2 ../../tools/finalization/nfs_observer.c -o nfs_observer_native
python3 ../../tools/finalization/test_observer.py ./nfs_observer_native ./light_monitor_native
```

前三组共18项测试，另有观察器回归。文件夹具、LD_PRELOAD错误注入与板端硬件故障是不同证据。

## 板端运行

把三个模块、应用、传感器/蜂鸣器加载脚本和beep_test放到板端 `/opt/light_monitor/`；应用启动脚本默认在该目录寻找设备运行文件。使用独立测试DTB启动后：

```sh
cd /opt/light_monitor
sh ./start_light_monitor.sh -l ./logs_demo
```

串口前台运行后输入STATUS、START、STOP、SET_INTERVAL或SET_THRESHOLD。Ctrl+C请求退出，STOP仅暂停采集。`tools/boot_lightbeep.ttl` 为历史TFTP/NFS启动模板，其IP、MAC和NFS路径需按实验网络调整；不执行saveenv。

`tools/finalization/` 与 `tools/i2c-nack-test/` 是历史验收/发布工具，保留真实测试实现。尤其NFS中断、驱动替换、安装脚本须先核对目标环境；它们不由普通构建自动执行。本次GitHub整理不重新运行故障注入或部署。
