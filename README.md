# i.MX6ULL Linux 光照采集与报警系统

基于 i.MX6ULL / 厂商 Linux 4.1.15 BSP 的嵌入式 Linux 项目，贯通设备驱动、设备树、多线程应用与板端故障验证。采集 AP3216C 光照原始值，用 KEY0 或串口控制启停，支持双阈值蜂鸣器报警和异步 CSV 日志。

## 功能与实现

- KEY0：platform 字符设备驱动，双边沿中断、约30ms延迟工作消抖，阻塞/非阻塞 read、poll、复制失败保留事件。
- AP3216C：I²C 字符接口，每次读取两字节小端 ALS 原始值；错误向上传递，不把旧值当作本次成功结果。
- 蜂鸣器：GPIO字符接口，独占访问、逻辑回读及错误报告，暂停、读取失败和退出时尝试关闭。
- 应用：控制、采集、日志三个线程；64条有界队列，满时丢弃新记录并计数；CSV轮换，默认最多5个10MiB文件。
- 控制：串口命令与KEY0共用业务入口，支持周期调整、双阈值滞回和状态查询；generation校验使状态变化期间的旧读取结果作废。

读数为原始值，未经校准，不称为lux。本项目不包含TCP业务，也不承诺硬实时或任意存储故障下零丢失。

## 目录

```text
drivers/lightkey/       KEY0事件驱动及专项测试
drivers/lightap3216c/   传感器驱动、工具与加载/恢复脚本
drivers/lightbeep/      蜂鸣器驱动与接口测试
apps/light_monitor/    多线程控制、采集和异步CSV应用
device-tree/           独立KEY0/蜂鸣器测试DTS
tools/finalization/    历史验收、观察、构建和交付工具
tools/i2c-nack-test/    独立真实地址无应答测试源码
docs/                  构建、硬件、验证范围及历史证据
```

## 构建与启动

需自行准备匹配的厂商BSP、ARM交叉编译器和板端运行环境；仓库不包含完整BSP、工具链或根文件系统。

```sh
export KERNEL_DIR=/path/to/vendor-linux
export CROSS_COMPILE=/path/to/toolchain/bin/arm-linux-gnueabihf-
make -C "$KERNEL_DIR" M="$PWD" ARCH=arm CROSS_COMPILE="$CROSS_COMPILE" modules
make -C apps/light_monitor CROSS_COMPILE="$CROSS_COMPILE" LDFLAGS=-static
```

按[构建与运行说明](docs/BUILD.md)编译，再按[完整部署指南](docs/DEPLOY.md)完成文件清单、备份安装、TFTP/NFS启动、板端验收及回滚准备。板端Linux启动后：

```sh
cd /opt/light_monitor
sh ./start_light_monitor.sh -l ./logs_demo
```

默认立即开始、每秒采集，阈值未设置时不启用报警。以下命令在应用前台运行时输入并回车；Ctrl+C请求退出，STOP仅暂停采集。

| 命令 | 行为 |
|---|---|
| STATUS | 查询采集有效性、状态及各类计数 |
| START / STOP | 恢复 / 暂停采集 |
| SET_INTERVAL MS | 周期100～60000ms |
| SET_THRESHOLD LOW HIGH | 0≤LOW<HIGH≤65535，低于LOW报警、高于HIGH解除、中间及等号保持 |

详细协议和并发语义见[应用说明](apps/light_monitor/README.md)。实验启动宏及历史验收脚本包含原实验网络的私有IP和路径，移植前须调整；普通构建不自动部署、解绑设备或修改网络。

## 已验证的结果

- KEY0事件、I²C正常读取与光照变化、蜂鸣器接口及声音、命令/按键控制、报警与日志轮换已有板端验证。
- 100ms周期约61秒：610次采样全部成功并保存，零遗漏、丢弃和相关错误。
- 12秒真实NFS端口中断：采样与查询继续，日志积压；恢复后197条全部报告保存，应用正常退出、测试规则清理完成。
- 真实I²C地址NACK：控制器返回-EIO，字符read=-1/errno5且缓冲区未覆盖；应用故障NA/报警关闭，恢复后继续采样和报警，最终恢复正式模块。
- 6采集+5日志+7控制，共18项原生进程测试通过；另有观察器回归。

[验证汇总](docs/VALIDATION.md)区分真实硬件、原生夹具和用户反馈。物理断线/断电、物理输出卡死、长期可靠性未证明；短时无丢弃不代表任意故障窗口零丢失。

## 开发与公开范围

KEY0主要由项目作者动手编写并进行板端联调，AI提供指导与调试支持；AP3216C、蜂鸣器首版和应用由AI按授权辅助实现，作者完成协作集成与实际板测。厂商BSP、引脚宏、gpiolib与I²C核心沿用现有环境。

