# 蜂鸣器开发前基线

用户板端输出：`/sys/class/leds` 包含 `beep`，其 device 链接指向 `/sys/devices/platform/leds`，trigger 为 `none`，brightness 为 0。挂载 debugfs 后 GPIO5 控制器覆盖全局 128–159，其中 gpio-129 为 `out lo`，消费者名称显示 `?`；此行本身不能独立证明归属或实际声音状态。

Ubuntu 实际板级 DTS 的 `/leds` 为 `gpio-leds`，其 `beep` 子节点使用 `gpios = <&gpio5 1 GPIO_ACTIVE_LOW>`、`default-state = "off"`；父节点 pinctrl 同时引用 `pinctrl_gpio_leds` 和 `pinctrl_beep`，后者配置 SNVS_TAMPER1 为 GPIO5_IO01。当前测试 DTS 包含该板级 DTS，并独立添加 lightkey。上述源码与板端 LED 设备相互印证，但运行时 DT 的完整属性仍可进一步核验。

独立蜂鸣器测试 DT 应保留现有 lightkey 和 I²C 配置，释放原 `/leds` 的 GPIO 与 pinctrl 后交由新节点申请；不能同时让两个驱动申请。改变测试 DT 前须核对其余 LED 子节点及影响，保留原 DTB 和启动方式。
