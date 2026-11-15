# KEY0 事件驱动

面向 i.MX6ULL / Linux 4.1.15 厂商 BSP 的 platform 字符设备驱动。设备树 compatible 为 `lightdemo,key0`，接口为 `/dev/lightkey`，KEY0 映射 GPIO1_IO18。

- 双边沿中断只调度延迟工作，约30ms后读取稳定状态，仅发布有效按下事件。
- 成功 `read` 返回二进制字节1；无事件时阻塞等待，非阻塞返回 `EAGAIN`。
- `poll` 报告可读而不消费事件；`copy_to_user` 失败返回 `EFAULT` 并保留事件。
- 一个待处理标志允许事件合并；限制一个独立打开实例，第二次打开返回 `EBUSY`。
- 卸载/解绑先停止新访问和IRQ，再等待延迟工作结束并唤醒读取者。

代码由本项目已验收版本复制整理，原始参考副本仍在本地保留。[构建说明](../../docs/BUILD.md)覆盖模块、工具和设备树；[验证汇总](../../docs/VALIDATION.md)说明板测范围。

`key_event_test.c` 在板端检查独占打开、非阻塞、poll与复制失败后保留事件。看到程序WAIT提示后再按实体按键，不要与主应用同时占用设备。
