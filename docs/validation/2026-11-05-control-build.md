# 统一控制首版构建与进程测试

2026-11-05用户授权直接开发任务08。实现位于apps/light_monitor：五条命令、KEY0有效事件切换、低于下限报警/高于上限解除的滞回、暂停/失败/退出关闭、输出失败独立统计及重试、CSV记录实际报警判断。

Ubuntu临时目录/tmp/light-control-v1；已运行6项采集回归、5项日志进程测试及7项新增控制进程测试，全部通过。新增测试覆盖：

- 两个阈值等号及中间区间保持、报警CSV与退出关闭。
- STOP不采样、START恢复、周期上下边界、非法参数不修改状态。
- 传感器暂时缺失时关闭，恢复时重新判断。
- FIFO一字节事件与串口共用TOGGLE业务入口。
- 原生适配调用边界注入关闭EIO，验证ERR applied=1、UNKNOWN及暂停重试恢复，累计错误保留。
- 阻塞在途读取后STOP，使旧低值作废，不能重新开启报警。
- 重启恢复1000ms默认周期、未启用阈值和运行状态。

原生夹具模拟的是协议和受控调用结果，不替代KEY0、声音、真实I2C或GPIO故障板测。两份LD_PRELOAD测试库均未编入ARM程序。

最终代码增加停止清理后的请求拒绝后，7项控制测试再次通过。静态ARM构建使用既有Linaro 4.9.4前缀及-std=gnu99 -Wall -Wextra -Werror -O2 -pthread -static，file确认ARM EABI5静态程序。临时目录和实际工程app构建产物cmp一致，SHA256：`bafc115acbec59ce0254c98cde49a00006d1d19d6e04a80954329571c5a9d804`。

实际工程备份为 `/home/jopwang/linux/IMX6ULL/Drivers/Linux_Drivers/light_monitor/app.before_control_20261105_231158`。部署脚本备份并更新NFS程序和start_light_monitor.sh，显示DEPLOYED: control-v1，部署哈希与构建一致。启动脚本及部署脚本语法检查通过；原模块和DTB均未修改。

当前尚未取得control-v1板端命令、按键及报警联调日志，任务08仍进行中。传感器真实I2C故障、物理关闭失败及NFS中断阻塞等未验收边界保持独立。
