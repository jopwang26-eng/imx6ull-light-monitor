# 2026-11-04：KEY0 消抖阶段反馈

用户在完成约 30ms delayed_work 消抖指导及测试步骤后回复“成功了”。记录为用户反馈消抖阶段通过，尚无本轮源码、构建输出或按键日志归档，不能声称为 AI 独立复测。

此前提供的测试覆盖：三次正常按下各产生一次 debounced press、持续按住不重复、松开后可再次识别按下、卸载及重新加载。此次反馈没有逐项原始输出。

当时 /dev/lightkey 仍是即时电平读取。下一阶段将改成单读取者的按下事件接口；阻塞/非阻塞 read、poll、事件消费及用户复制失败行为尚未取得实测结果。

## 事件读取阶段发现的失败输出

用户随后提供：

```text
/ # od -An -N1 -tu1 /dev/lightkey
   0
/ # [13637.323608] lightkey: debounced press
[13638.613600] lightkey: debounced press
[13639.723600] lightkey: debounced press
[13640.373591] lightkey: debounced press
[13640.923598] lightkey: debounced press
```

od 在后续按键日志之前已返回字节0并退出，与事件版“无事件阻塞、成功数据为1”的约定不符。当前运行行为与旧电平接口一致，但尚不能仅凭输出确定是未重载、未更新部署文件，还是实际源码/read操作表未替换。先核对编译、NFS复制及卸载重载链路，再以相同od命令复测；若仍返回0，检查实际lightkey_read及lightkey_fops。消抖打印本身不证明事件read已生效。

## 完整源码提供后的重载结果

用户再次提供：

```text
rmmod lightkey
[14892.306083] lightkey-demo lightkey-test: lightkey remove
/ # insmod /opt/light_monitor/lightkey.ko
[14909.808681] lightkey-demo lightkey-test: /dev/lightkey registered, IRQ=47, debounce=30ms
/ # od -An -N1 -tu1 /dev/lightkey
   0
/ # [14924.513599] lightkey: debounced press
[14925.543601] lightkey: debounced press
[14925.983601] lightkey: debounced press
```

这次已经有卸载、重新加载的直接证据，不能继续把“忘记卸载”当作首要解释。完整整合源码的成功日志为 `/dev/lightkey event-v1 ready, IRQ=%d, debounce=%dms`，此次却仍为旧日志 `registered`，确认实际运行的构建未采用提供的完整整合版本。根因仍需沿实际 Ubuntu 源码、构建输出 ko、NFS 部署 ko 三处的版本标记定位；如果三处都有标记，再核对板端实际挂载与运行文件。事件读取仍未通过，不能仅凭发布函数或 read 源码正确而声称已修复。
