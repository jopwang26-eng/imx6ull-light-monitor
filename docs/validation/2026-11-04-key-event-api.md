# 2026-11-04：按键事件接口专项实测

## 用户提供的板端输出

```text
/ # /opt/light_monitor/key_event_test
PASS: second open -> EBUSY
PASS: empty nonblocking read -> EAGAIN
CHECK: keep KEY0 released for one second
PASS: empty poll -> timeout
WAIT: press KEY0; Ctrl+C to exit
[18611.823645] lightkey: debounced press
PASS: invalid buffer -> EFAULT
PASS: event #1 = 1, consumed -> EAGAIN
WAIT: next press; holding KEY0 should not repeat
[18614.123636] lightkey: debounced press
PASS: event #2 = 1, consumed -> EAGAIN
WAIT: next press; holding KEY0 should not repeat
[18615.853627] lightkey: debounced press
PASS: event #3 = 1, consumed -> EAGAIN
WAIT: next press; holding KEY0 should not repeat
[18618.863628] lightkey: debounced press
PASS: event #4 = 1, consumed -> EAGAIN
WAIT: next press; holding KEY0 should not repeat
```

## 已有证据支持的行为

- 第二次独立 open 返回 EBUSY。
- 无待处理事件时非阻塞 read 返回 EAGAIN，无事件 poll 按一秒超时。
- 有效按下可唤醒 poll；工具在 read 前再次 poll，若就绪丢失会输出 FAIL，当前日志表明此检查已通过，poll 本身不消费事件。
- 空缓冲区读取返回 EFAULT，随后同一事件仍能正常读出字节1，说明失败时没有错误消费。
- 连续四次正常事件均能读取，成功消费后立即再次 read 得到 EAGAIN，并继续等待。

四次事件与此前“三次正常按下及一次长按”的安排一致，但用户没有在原始输出中逐次标记物理操作，因此不将日志单独当成精确按住时长证明。连续读取和长按不重复另有用户成功反馈。

## 第一轮结束时尚待补齐

本轮尚未贴出 Ctrl+C 后 EXIT: status=0、事件版卸载重载及“读取程序关闭时三次按下合并为一个待处理事件”的输出。任务03保持进行中。

下一轮先退出工具，卸载重载，再在没有读取程序时按下并松开三次，随后启动工具。预期先报告 cleared one previously pending event，紧接 empty nonblocking read -> EAGAIN；看到 WAIT 后再按一次验证重新加载后的事件路径，最后 Ctrl+C 获取正常退出输出。

## 第二轮收尾原始输出

```text
^C/ # rmmod lightkey
[19007.900628] lightkey-demo lightkey-test: lightkey remove
/ # insmod /opt/light_monitor/lightkey.ko
[19018.065272] lightkey-demo lightkey-test: /dev/lightkey event-v1 ready, IRQ=47, debounce=30ms
/ # [19030.563612] lightkey: debounced press
[19032.103606] lightkey: debounced press
[19033.693601] lightkey: debounced press
/opt/light_monitor/key_event_test
PASS: second open -> EBUSY
INFO: cleared one previously pending event
PASS: empty nonblocking read -> EAGAIN
CHECK: keep KEY0 released for one second
PASS: empty poll -> timeout
WAIT: press KEY0; Ctrl+C to exit
[19062.733613] lightkey: debounced press
PASS: invalid buffer -> EFAULT
PASS: event #1 = 1, consumed -> EAGAIN
WAIT: next press; holding KEY0 should not repeat
[19063.713635] lightkey: debounced press
PASS: event #2 = 1, consumed -> EAGAIN
WAIT: next press; holding KEY0 should not repeat
[19064.473631] lightkey: debounced press
PASS: event #3 = 1, consumed -> EAGAIN
WAIT: next press; holding KEY0 should not repeat
```

第二轮支持：Ctrl+C 后返回 shell 并可成功卸载，说明读取程序已停止且不再持有模块；remove/probe 能重载 event-v1 并恢复正常事件功能。没有读取者时的三条按下日志，在启动工具后只消费一个旧事件，随后立即 EAGAIN，证明单待处理标志允许合并的约定成立。

结合此前两态读取和连续阻塞读取的用户成功反馈、实际源码审查与本页原始专项输出，按键阶段任务02/03的验收范围已覆盖。未归档工具的 EXIT 行，不宣称观察到进程退出码0；也未进行长时间压力或掉电测试，阶段验收不扩展为生产可靠性结论。
