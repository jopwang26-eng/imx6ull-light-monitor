# 蜂鸣器首版 probe 失败诊断

用户板端两次执行 `sh ./load_lightbeep.sh`，均显示 `lightbeep lightbeep-test: GPIO initialization failed: -5`、probe failed，加载脚本检查失败后卸载模块；`beep_test status` 因字符节点不存在失败。一次串口粘贴把 `beep_api_test` 和 echo 拼成一个命令，边界测试实际未运行。

已确认 platform 匹配发生，但初始化尚未通过。不能将本轮记录为默认关闭或声音验收通过。

读取实际 BSP 确认 gpio-mxc 使用 GPIO_PSR 作为 bgpio 的输入回读寄存器，方向输出写 GPIO_DR/GDIR；逻辑值由 gpiolib 按 ACTIVE_LOW 反相。当前驱动在关闭输出后立即比较回读，回读不符也生成 EIO。故需区分输出配置失败、输入采样与输出不同、复用配置问题。

由实际 imx6ull.dtsi 和 gpio-mxc.c 核对：GPIO5 基址 0x020ac000，DR/GDIR/PSR 偏移 0/4/8；iomuxc-snvs 基址 0x02290000，SNVS_TAMPER1 GPIO5_IO01 的 mux/pad 偏移 0x0c/0x50。

用户板端只读结果依次为 DR=0x00000FE2、GDIR=0x00000F8A、PSR=0x00000060、mux=0、pad=0x00017099。bit1 显示输出值为1且为输出模式，但采样为0。这些数值在 probe 失败并卸载后取得，不能独立证明 probe 执行时的复用配置或根因。

增加 beep-v1-diag 定向日志：区分 devm_gpiod_get、direction_output 返回错误和关闭后回读不符；回读不符时延后1–2ms再观测，但仍按首次不符返回EIO，不掩盖原故障。尚未改 DTB，需板端诊断日志后再修复；诊断日志应在最终收尾时移除。

用户部署诊断模块后取得：`requested=0 first_logical=1 delayed_logical=1 active_low=1 gpio=129`，随后仍初始化 EIO。确认申请和方向输出调用成功，延后1–2ms仍不符；不是该等待范围内可消除的即时回读问题。

下一次单变量对照只改变蜂鸣器测试 DTS 的 pinctrl_beep 配置：0x17059 → 0x40017059。BSP pinctrl-imx.c 将 bit30 解析为 mux SION，使输入通路强制启用。诊断模块和回读校验保持原样。此为待板测的候选修复，不宣称根因或修复成功；原测试 DTS/DTB 已保存在 Ubuntu /tmp/lightbeep/before-sion.dts 与 before-sion.dtb。

用户部署 SION 对照 DTB 并重新启动后，加载日志出现 `beep-v1 ready: /dev/lightbeep default=off`，加载脚本及随后单独执行的 status 均显示 `GPIO_LOGICAL=0 (off)`。诊断模块未改变回读校验，仅修改引脚 SION 配置后原复现通过，支持输入通路配置为此次回读失败的原因。该结论不将失败后 mux=0 的读数解释为 probe 当时的状态。

初始化与默认关闭逻辑回读已通过；声音、API 边界、卸载关闭及重载仍待板测，临时诊断代码待收尾清理。

用户随后提供完整 API 输出，独占打开 EBUSY、非法值/长度 EINVAL、无效读写指针 EFAULT、零长度读写、非法请求后保持关闭、关闭及重新打开均 PASS，`failures=0`、`api_exit=0`。on 回读1、off回读0；用户明确反馈“正常响了”，确认实物发声。卸载停止和重载后的默认无声尚需实物确认。

已清理临时 `[beep-diag]` 日志和延后采样，正式版本标记 beep-v1.1，保留即时回读校验及测试 DTS 的 SION。正式清理版需要重新部署后复测，不能用诊断版结果冒充正式版已上板。

后续已收到正式版初始化、API、卸载关闭与重载日志及用户成功反馈，详见[最终板测记录](2026-11-05-beep-board.md)。临时诊断代码已清理，原失败复现以正式版加载通过作为回归结果。
