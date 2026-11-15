# 硬件连接与资源

本项目按底板器件→连接器针脚→核心板SoC PAD核对连线，再以实际BSP与板端状态验证。

| 功能 | 连接 | SoC资源 |
|---|---|---|
| KEY0 | 底板J2-48，UART1_CTS | UART1_CTS_B，GPIO1_IO18 |
| AP3216C SCL/SDA | J2-34/J2-35 | UART4_TX_DATA/UART4_RX_DATA的I2C1功能 |
| AP3216C INT | J2-49，AP_INT | GPIO1_IO01；本项目不使用该中断 |
| 蜂鸣器 | J2-5，SNVS_TAMPER1 | GPIO5_IO01，低有效 |

KEY0不是GPIO1_IO01。测试DTS停用原gpio-keys，避免重复申请GPIO1_IO18；传感器由实际client `0-001e` 接管，不整体停用共享I²C控制器。

蜂鸣器测试DTS释放原leds节点中的蜂鸣器资源，并启用SION以保留输入通路供GPIO逻辑回读。只修改独立测试DTS，不替换厂商原始板级文件或永久U-Boot环境。SION故障对照和最终板测见 [诊断记录](validation/2026-11-05-beep-probe-failure.md)。

图纸版本、焊接选项与外接设备可能不同；移植到其他板卡前应重新核对。原理图PDF及本地逐页截图不随此仓库发布。
