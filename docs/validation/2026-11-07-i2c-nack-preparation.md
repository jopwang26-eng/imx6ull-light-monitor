# I²C真实传输错误测试准备

2026-11-07，用户授权继续最后一项测试，实际板端执行尚待输出。

用户确认没有额外I²C模块或扩展板。板端设备列表为0-000e、0-001e、1-001a、1-0038、1-003c、1-005d；0-001e绑定lightap3216c；/dev/i2c-0及1存在。0-001f没有已注册client；不将注册列表当完整总线扫描。

实际BSP配置CONFIG_I2C_CHARDEV=y，CONFIG_KPROBES与CONFIG_FAULT_INJECTION未启用。i2c-imx.c中i2c_imx_acked检测I2SR_RXAK后返回-EIO（No ACK）。采用独立包装模块，在ALS低字节read入口可切换成一次0x1f单字节只读事务，真实负返回进入原驱动read；若意外应答，返回EPROTO并判失败。正常读取及初始化、关闭仍使用0x1e。不通过软件直接伪造EIO，不操作复用引脚或整体停用控制器。

源文件位于tools/i2c-nack-test/。Ubuntu临时构建目录 `/tmp/lightap-nack-build.NVVsZK`。实际Linux4.1 BSP模块构建通过，用户态check_nack为ARM EABI5静态程序，脚本sh -n通过。正式源及ko哈希保持9fa54171fcbe54483721fe8ec45000a650eb05b8eb6cae2bb8e38b7fc2286ba9、487b63d052cde42104b7a485b9461b4051d5a527ee8eb3c2c607a26edc1108a1。

测试模块SHA256=b326f2cc344c10f16f30c7ecbce670d57c66697b07764199e9962a6ddea0f45c；check_nack SHA256=18f856ab344e556f322b6fa596f86a3940f4003898aafa8fb21e827f431e41de。安装到独立i2c-nack-test目录需用户Ubuntu交互sudo，不覆盖正式模块。

板端脚本先检查应用未运行及测试地址未注册，临时替换模块；在同一fd验证正常、errno5且用户缓冲区哨兵未变、恢复，再验证应用NA/报警关闭及恢复；退出trap尝试恢复正式模块并重读3次、校验正式文件哈希。任何失败都保留非零退出码与报告。此为真实适配器地址无应答的错误路径验证，不宣称AP3216C实物断线、掉电或物理总线故障已测。完成结论须依据板端输出；当前任务04仍未验收。

后续板测已完成并通过，TEST_EXIT=0/script_exit=0，正式模块恢复正常，见[最终专项记录](2026-11-07-i2c-nack-board.md)。本文“尚待”描述工具准备时刻，不再作为当前待办。
