# 周期采集首版构建与应用测试

2026-11-05：按用户授权直接实现 `apps/light_monitor/`，Ubuntu临时验证目录 `/tmp/light-sampling-v1`，实际工程 `/home/jopwang/linux/IMX6ULL/Drivers/Linux_Drivers/light_monitor/app`。上层内核模块文件和Makefile不变。

`make native` 使用 `-std=gnu99 -Wall -Wextra -Werror -O2 -pthread`，`python3 tests/test_process.py ./light_monitor_native` 输出 `Ran 6 tests`、`OK`。覆盖默认1000ms采样、半条命令不阻塞采集、STATUS、缺失设备与短帧标记无效及恢复、非法/超长/NUL命令、SIGSTOP/SIGCONT产生遗漏而不突发补采、60000ms周期下Ctrl+C唤醒、独立EOF退出及非法周期参数。修改EOF测试确保由EOF本身退出后重新执行，通过。

测试使用普通文件协议夹具和真实应用进程，不是I2C硬件故障实测。传感器任务04故障验收仍未完成。应用目前尚未进行真实板端串口与周期验证。

实际工程使用既有Linaro 4.9.4工具链静态交叉编译成功，file显示 `ELF 32-bit LSB executable, ARM, EABI5 ... statically linked`。实际工程与临时目录可执行文件cmp一致，SHA-256为 `fec68ce6a0c7068ba4f7df921e894fe9266d8b32af3a901ec7556a81f04db30f`。

部署脚本为实际应用目录的 `deploy_sampling.sh`，语法检查通过；备份同名旧程序，只安装应用到NFS `/opt/light_monitor/light_monitor`。部署结果及板测结果需以实际输出追加。

已通过交互式终端完成部署，脚本显示 `DEPLOYED: sampling-v1`，实际工程与NFS程序cmp一致，两份SHA-256均为上述 `fec68c…db30f`。板端应用运行尚待实测。
