# I²C 地址无应答错误传播板测

2026-11-07，依据用户提交的完整板端输出验收。报告目录 `/opt/light_monitor/i2c-nack-report.C8P9ji`；原始串口输出随包保存为 `2026-11-07-i2c-nack-raw.txt`。

## 方法及边界

独立lightap_nack_test模块包含正式驱动源副本，正常模式直接使用正式事务。开启测试后，仅在ALS低字节read入口对总线0的0x1f地址发一次单字节只读事务；真实i2c_transfer负返回原样交给正式read错误路径，意外ACK报EPROTO并判失败。用户确认无额外I²C模块；0x1f无注册client。没有软件直接伪造EIO，没有写0x1f寄存器或操作共享控制器/引脚。

实际BSP i2c_imx_acked在RXAK无应答时返回-EIO；板端内核连续记录 `LIGHTAP_NACK_TEST bus=0 addr=0x1f read_only=1 hardware_ret=-5`。此证据为真实适配器地址无应答错误路径，不能称为AP3216C实物断线、断电或总线被拉死。

## 同一fd的字符接口检查

```text
DIRECT stage=normal read_ret=2 errno=0 bytes=00,00
DIRECT stage=nack read_ret=-1 errno=5 bytes=a5,5a
DIRECT stage=recovered read_ret=2 errno=0 bytes=00,00
DIRECT_RESULT=0
```

失败返回可识别EIO，用户缓冲区哨兵未被覆盖，没有把旧值返回为本次成功样本。关闭注入后同一fd恢复正常读取；该时刻原始值为0，不能换算为校准lux。

## 正式应用与恢复结果

- 正常读取raw=0，阈值65534/65535，alarm=ON。
- 约2.209秒起连续10次读取失败，errno=5；故障STATUS为valid=0、ALS_RAW=NA、successes=11、errors=5、last_errno=5、alarm=OFF、buzzer=OFF，控制及日志错误均0。
- 约4.203秒恢复有效读取；6.003秒STATUS为valid=1、successes=21、errors=10、last_errno=0、alarm=ON、buzzer=ON，显示自动恢复后重新判断报警。
- 应用EXIT：attempts=36、successes=26、errors=10、missed=0、log_saved=26、log_dropped=0、log_errors=0、log_failed=0、log_uncertain=0、control_errors=0、key_errors=0、buzzer=OFF。失败尝试未计作成功记录，26次成功全部报告保存；未另行独立核对CSV行数。
- 退出清理卸载测试模块并恢复正式als-v1模块，重新probe成功；随后lightap_test连续3次读成功。正式lightap3216c.ko哈希校验OK，蜂鸣器GPIO_LOGICAL=0。
- TEST_EXIT=0，紧接脚本的Shell输出script_exit=0。脚本只有测试模块卸载、正式模块恢复/绑定、重新读取及哈希检查成功才保留0。

## 验收结论

任务04的“I²C读取失败返回可识别错误、用户程序不把旧值视作成功”及恢复标准通过；结合已归档的正常光照变化、初始化/重载、短缓冲区及原驱动恢复，任务04阶段验收完成。任务01至09的既定范围已完成，物理断线/掉电、物理输出卡死、长期可靠性及任意NFS故障零丢失仍不在已证明范围内。
