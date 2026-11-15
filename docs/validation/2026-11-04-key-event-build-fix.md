# 2026-11-04：事件驱动编译与部署链路修复

## 实际工程与连接

通过 VS Code 窗口确认远程工程是 /home/jopwang/linux/IMX6ULL/Drivers/Linux_Drivers/light_monitor，SSH 用户为 jopwang，地址 192.168.220.131。随后通过 SSH 直接读取源码、Makefile 和构建记录。

实际网卡输出确认同一 Ubuntu 主机同时持有 ens33 的 192.168.220.131 和 ens37 的 192.168.50.130；/etc/exports 导出 /home/jopwang/linux/nfs，使用 rw,sync,no_root_squash。此为实际输出，不是据旧地址猜测。

## 已确认的问题

1. 完整 event-v1 源码已保存，但模块未更新。排查时源码修改时间为 2026-11-04 15:32:31 +0800，工程 lightkey.ko 为 14:50:40；NFS 副本虽于 15:33:03 更新，哈希仍与旧构建相同。两个旧文件都只包含 registered 日志，不含 event-v1 ready。
2. 对实际 BSP 重新编译，复现 TASK_INTERRUPTIBLE、TASK_KILLABLE 未声明及 schedule 隐式声明错误。提供的完整参考代码遗漏 linux/sched.h，这是必须修正的 Linux 4.1 头文件依赖。

此前用户提供的 lightkey_read、消抖回调和 fops 的事件逻辑本身与约定相符。不能仅重写这三段解决旧构建被部署的问题。

## 修复和验证

- 在实际 Ubuntu lightkey.c 中补充带中文注释的 #include <linux/sched.h>，同步修正 Windows 完整源码和可复制文档；未改动按键事件业务逻辑。
- 保留旧源码和两个旧模块，备份目录为 Ubuntu /tmp/lightkey-event-v1.lqvG9X。该临时备份可能随系统清理而消失。
- 使用实际已安装的交叉工具链绝对前缀 /usr/local/arm/gcc-linaro-4.9.4-2017.01-x86_64_arm-linux-gnueabihf/bin/arm-linux-gnueabihf-，在用户指定内核树构建。CC、MODPOST 1 modules、LD 均成功，退出码0。
- file 确认输出为 ELF 32-bit LSB relocatable, ARM, EABI5。
- strings 确认新模块含 version=event-v1 和 /dev/lightkey event-v1 ready, IRQ=%d, debounce=%dms。
- 将新模块部署到 /home/jopwang/linux/nfs/rootfs/opt/light_monitor/lightkey.ko。cmp 检查通过，构建文件与 NFS 副本 SHA-256 一致。

旧模块 SHA-256：

```text
daf07101e39e7910b9d10f204ce89efc3b5b4431911fed2a2019ce8894c97093
```

新模块及 NFS 副本 SHA-256：

```text
6fd803fc8bc33b158d4c7a8528f97fad9ea8826b8ed55491407c045513982c8c
```

## 当前断点

用户明确表示上板测试等回来再进行。本轮没有操作开发板卸载、加载或读取，不将事件驱动板端功能记为通过。NFS 文件更新不会自动替换内存中已经加载的旧驱动。

回来后先结束旧读取程序，再在板端执行：

```sh
# 卸载内存中仍可能运行的旧模块，成功后再继续
rmmod lightkey

# 松开 KEY0，加载已经部署好的新版
insmod /opt/light_monitor/lightkey.ko

# 无事件时应等待；按下后显示字节1并退出
od -An -N1 -tu1 /dev/lightkey
```

加载日志应含 event-v1 ready。随后验证再次等待、长按不重复、Ctrl+C、非阻塞 EAGAIN、poll、单独重复打开、事件合并、复制失败后事件保留和卸载重载。

## 后续板端反馈

用户回来后反馈“按一次显示1，之后显示lightkey: debounced press”。已解释 od -N1 每次只读取一个字节并退出，后续只有内核日志不代表读取失败。随后用户对连续 od 循环测试回复“成功了”。据此将单次及重复事件读取记录为用户反馈通过；该轮没有完整原始日志，非阻塞/poll及异常路径仍待专项测试。
