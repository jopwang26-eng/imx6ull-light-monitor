# 2026-11-03：KEY0 引脚映射纠正

## 结论

此前将底板 KEY0 判断为 GPIO1_IO01 有误。复核底板原理图第 5 页，J2 第 48 脚的底板 KEY0 接 UART1_CTS；用户提供的板级 DTS 将 UART1_CTS_B 复用为 GPIO1_IO18。底板第 1 页显示 KEY0 上拉到 3.3 V，按下接地。

此前误将核心板连接器图中的功能标注直接当作底板 KEY0 的实际连线，忽略了底板侧 J2 的具体接线。底板第 5 页标明 J2 49 脚的 GPIO_1 与 J2 48 脚的 UART1_CTS/KEY0 是两条不同线路。

## 设备树及板端证据

用户提供的板级 DTS 配置：

```dts
gpio_keys: gpio_keys@0 {
    compatible = "gpio-keys";
    pinctrl-0 = <&pinctrl_gpio_keys>;
    key1@1 {
        label = "USER-KEY1";
        gpios = <&gpio1 18 GPIO_ACTIVE_LOW>;
    };
};

pinctrl_gpio_keys: gpio-keys {
    fsl,pins = <
        MX6UL_PAD_UART1_CTS_B__GPIO1_IO18 0x80000000
    >;
};
```

这段摘录仅保留相关字段。`USER-KEY1` 是设备树中的 label，不能直接当作底板按键丝印名称。

用户按此前“松开、按住”顺序提供的 debugfs 输出中，首份和第二份的相关行分别为：

```text
gpio-1   (key                 ) in  hi
gpio-18  (USER-KEY1           ) in  hi

gpio-1   (key                 ) in  hi
gpio-18  (USER-KEY1           ) in  lo
```

若第二份确为按住底板 KEY0，电平变化与底板 J2 48 脚的映射一致；GPIO1_IO01 在这两次观察中没有变化。此前 `pressed=0` 两次对应的是错误的 GPIO1_IO01，不能视为 KEY0 输入测试通过。

## 后续处理

当前运行的 gpio-keys 已申请 GPIO1_IO18。只在独立测试设备树中将该节点标记 disabled，并让 lightkey 使用独立 pinctrl 组及 GPIO1_IO18；不修改原板级设备树或原启动 DTB。重启后需重新验证加载、松开和按住的实际值；切回原设备树后原 gpio-keys 配置恢复。

## 后续用户反馈

2026-11-03 用户先确认收到原节点的停用状态，随后针对“松开/按住分别加载模块，预期 pressed=0/1”的测试回复“成功了”。据此将 GPIO 输入阶段记录为用户反馈通过；尚无此次完整原始日志，不能将这条反馈扩展为字符设备或中断功能已完成。
