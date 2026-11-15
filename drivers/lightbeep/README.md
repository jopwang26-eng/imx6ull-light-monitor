# 蜂鸣器 beep-v1.1

本实现由用户授权 AI 直接完成首版。底板 BEEP 经 J2-5 连接 SNVS_TAMPER1 / GPIO5_IO01，低电平有效。GPIO 描述符接口按设备树的 `GPIO_ACTIVE_LOW` 自动反相，驱动仅使用逻辑 0（关）和 1（开）。

测试 DTS 为该引脚配置 SION，保持输入采样通路，供 GPIO PSR 回读检查输出。旧配置未启用时，板端关闭输出后仍回读为逻辑1而导致 EIO；仅启用 SION 后初始化、回读和接口测试通过，用户确认发声。定位记录见[板端诊断](../../docs/validation/2026-11-05-beep-probe-failure.md)。v1.1 已移除临时诊断日志和延后采样，保留原回读校验。

## 接口

模块 `lightbeep.ko`，platform 驱动名 `lightbeep`，compatible 为 `lightdemo,beep`，字符节点 `/dev/lightbeep`。独占打开；持有文件时普通卸载返回忙。`write` 仅接受一个二进制字节 0 或 1（不是 ASCII 字符），成功返回 1。非零错误长度或非法值返回 `EINVAL`，用户指针无效返回 `EFAULT`，零长度返回 0。`read` 返回一字节 GPIO 逻辑回读值，不提供声音检测。

控制使用有返回值的 `gpiod_direction_output` 并检查逻辑回读：下层错误向上传递，回读不符返回 `EIO`。成功仅表示 GPIO 请求及回读符合预期。断线、器件故障、实际声音仍须实物验证。文件关闭保持输出，方便独立 on/off 命令；应用退出前必须显式关闭。初始化、模块卸载和系统关机时尝试关闭；卸载和关机期间的关闭失败写入内核日志，不能阻止已经开始的驱动解绑。

手动解绑后旧文件返回 `ENODEV`；旧文件关闭前拒绝重新绑定，避免跨代访问。采用 mutex 串行化读取、输出和解绑，GPIO 由 devm 管理；注册失败不发布可访问状态。

## 构建与部署

Windows 本目录与 Ubuntu 源码目录分别管理，当前实际工程为 `/home/jopwang/linux/IMX6ULL/Drivers/Linux_Drivers/light_monitor`。实现已在实际 Linux 4.1.15 BSP 编译；详见 [构建记录](../../docs/validation/2026-11-04-beep-build.md)。Ubuntu 运行 `sh <实际工程>/deploy_ubuntu.sh`，交互 sudo 认证后备份并部署 NFS 文件与独立 TFTP DTB。

新 DTS 继承已有 lightkey 测试 DTS，并停用原 `/leds`，原板级 `/leds` 目前仅含 beep。保留原按键测试 DTB，用于恢复。新 DTB 在加载蜂鸣器模块前不保证 GPIO 输出状态；本驱动承诺的是 probe 初始化关闭，不承诺上电至 probe 前无瞬态。恢复原测试 DTB 并重启可恢复 gpio-leds 管理方式。

## 板端验证

先用新 `imx6ull-alientek-emmc-lightbeep-test.dtb` 启动；可在现有 Tera Term U-Boot 会话执行 [启动宏](../../tools/boot_lightbeep.ttl)，沿用已有 TFTP/NFS 参数。宏不保存永久启动环境。

```sh
cd /opt/light_monitor
sh ./load_lightbeep.sh
./beep_test status
./beep_api_test
echo "api_exit=$?"
./beep_test on
sleep 1
./beep_test off
```

用户确认默认无声、on 有声、off 停止。API 测试保持关闭，验证独占打开、非法长度/值、EFAULT、零长度及重新打开，不能代替声音或关闭故障实测。接着 on 后执行 `rmmod lightbeep`，核对 remove 日志与实物停止；再运行加载脚本确认可重复初始化关闭。测试后如需保持关闭，运行 off，或正常卸载。

原生 `python3 test_cli.py ./beep_test_native` 使用普通文件夹具验证工具字节协议和错误退出，不模拟 GPIO，不能当作板测通过。两份测试程序均为静态 ARM EABI5。
