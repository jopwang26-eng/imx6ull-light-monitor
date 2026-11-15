# 异步CSV首版构建与测试

2026-11-05：新增logger.c/logger.h并接入采集成功记录，采用64条有界队列、独立日志线程、默认10MiB且最多五个CSV、约5秒存储错误重试。退出先停采集再排空，丢弃及失败如实报告。

Ubuntu临时目录 `/tmp/light-logging-v1` 的 `make native` 和原有6项采集进程测试通过。新增5项日志进程测试输出 `Ran 5 tests in 15.823s`、`OK`：

- CSV内容、序号、退出排空及重启追加保留；成功记录与保存数核对。
- 128字节小上限触发五文件轮换，文件大小与历史顺序符合预期。
- 父目录缺失时采集及STATUS继续；未提前重试，约5秒后新建目录恢复，累计丢弃保留。
- RLIMIT_FSIZE触发真实短写/EFBIG，失败行回退后CSV仅保留已保存完整行。
- 测试专用LD_PRELOAD延迟fdatasync，让队列达到64并丢弃新记录，解除延迟后排空，保留先入队64条，退出时successes=saved+dropped。

原生夹具验证应用和文件系统错误处理，不代表板端NFS故障已实测。测试延迟库不链接进正式程序。

实际工程app目录备份于 `/home/jopwang/linux/IMX6ULL/Drivers/Linux_Drivers/light_monitor/app.before_logging_20261105_224601`。临时目录及实际工程均用既有Linaro 4.9.4工具链、`-std=gnu99 -Wall -Wextra -Werror -O2 -pthread -static`构建成功。file显示ARM EABI5静态程序，两份cmp一致，SHA256为 `c53a80d4f55b0a3edcbc01c6084eea673fdb1187b549b21fb0557ca05b2cffb1`。

通过备份部署脚本已更新NFS `/opt/light_monitor/light_monitor`，脚本显示 `DEPLOYED: logging-v1`，部署与构建哈希一致。真实板端CSV保存、STATUS统计及轮换还待验证，任务07尚未完成。
