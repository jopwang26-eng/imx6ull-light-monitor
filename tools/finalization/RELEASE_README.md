# light_monitor 阶段验收交付包

2026-11-07任务01至09在既定范围内完成阶段验收。已完成12秒真实NFS端口中断观察及真实I²C地址NACK错误传播/恢复，详见validation/专项记录；AP3216C实物断线、断电及物理输出失效未实测。源码由用户与AI协作完成：KEY0开发主要由用户实施并由AI支持调试；AP3216C、蜂鸣器首版和应用由AI按用户授权实现。原厂BSP、引脚宏、gpiolib和I2C核心接口沿用厂商环境，不能宣称全部底层均自行编写。

## 包内内容

- bin：三个模块、control-v1静态应用、测试工具、独立DTB、加载/恢复与最终验收脚本。
- source：实际Ubuntu工程的驱动与应用源码、用户态测试、设备树源码及构建工具。
- build-environment.txt：本次Ubuntu宿主、交叉编译器、目标模块版本及内核配置/接口哈希。宿主内核不等于板端内核。
- MANIFEST.sha256：包内源码和产物的完整性清单；测试成功时另有native-tests.txt。
- validation：已归档的板端证据和验收范围，包含NFS中断及恢复结果。
- test-only：本轮已经板测的独立NACK模块、静态检查程序和板端脚本；普通安装器不会安装此目录。

依赖既有Linux4.1.15厂商BSP构建树及Linaro4.9.4工具链，不包含完整BSP。device-tree内的测试DTS包含原板级DTS，后续include和生成头文件仍依赖同一BSP。模块版本与板端内核必须一致。

## 重建

将source中的驱动C和Makefile放入独立工程目录，app保持子目录；将source/tools复制为工程tools子目录，将包内validation目录复制到工程根目录，三个板级/测试DTS放入BSP arch/arm/boot/dts，先备份同名文件。设置LM_PROJECT_DIR、LM_KERNEL_DIR、LM_CROSS_COMPILE后运行 `sh tools/build_release.sh --test`。默认环境为本项目已确认的Ubuntu路径，工具链变量是以arm-linux-gnueabihf-结尾的前缀。脚本仅重建打包，不自动部署。

重新打包时，将test-only/i2c-nack-test内的已板测产物复制至工程tools/i2c-nack-test同名目录；正式模块与应用由构建脚本重建，NACK测试产物作为已板测附件携带。测试工具另行重编译可运行该子目录build_ubuntu.sh；其安装器固定校验本轮已板测的两项指纹，如编译环境导致指纹变化须先核对再调整，不跳过检查。

检查 `sha256sum -c MANIFEST.sha256`。确认板端应用已经退出后，在Ubuntu以 `sudo sh install_release.sh <本包目录>` 备份并安装到既定NFS/TFTP路径。安装器不运行saveenv，不安装内核zImage，不替换日志数据。旧模块若仍在内存中，安装新文件不等于已加载，仍须重载或重启验证。

## 启动与演示

板端沿用boot_lightbeep.ttl从U-Boot临时TFTP加载zImage和本测试DTB，以NFS rootfs启动。进入Linux后：

```sh
cd /opt/light_monitor
sh ./start_light_monitor.sh -l ./logs_demo
```

默认每秒采集且报警阈值未启用。应用内部命令为STATUS、START、STOP、SET_INTERVAL MS、SET_THRESHOLD LOW HIGH；每条按回车提交。STOP暂停、START恢复；KEY0有效按下切换同一状态。Ctrl+C退出后回到Shell，才可输入Linux命令。

范围：周期100–60000ms，阈值0≤LOW<HIGH≤65535，低于LOW报警、高于HIGH解除，等号及中间保持。应用启动参数、状态字段、错误与日志语义详见source/app/README.md。不做校准lux、不开发TCP、不承诺任意NFS故障下立即退出或掉电零丢失。

## 最终测试与边界

collect_board_env.sh只读采集实际板端内核和挂载、驱动绑定与文件哈希。soak_board.sh默认60秒正常存储测试；test_buzzer_unavailable.sh通过临时解绑输出驱动验证应用的失败报告和恢复，不能等同物理引脚卡死。

NFS测试先运行prepare_nfs_board.sh，把静态应用与观察器复制到专用tmpfs；板端BusyBox动态链接，故障期间不能依赖它从NFS继续启动命令。用户准备连续操作时，先在Ubuntu运行nfs_interrupt_ubuntu.sh --run READY_HOST_PATH等待新标记，看到WAITING_FOR_BOARD后在板端启动prepare打印的观察命令。观察器约34秒发布标记，主机才开始12秒中断；主机预登记20秒独立恢复。观察器发送STATUS/STOP/START，再SIGINT退出；本次STOP/START位于中断前，不能称为中断中控制验收。根文件系统也来自NFS，客户端RPC重试可能在流量恢复后继续等待。本轮积压期间采样查询继续，恢复后197条全部报告保存，应用退出0、规则清理完成；未独立核对CSV行数，不承诺任意故障时长零丢失或立即退出。

未附入成功日志的项目不得写成“已通过”。模拟/原生进程夹具、板端GPIO回读、实物声音反馈、真实NFS中断是不同证据，不能相互替代。
