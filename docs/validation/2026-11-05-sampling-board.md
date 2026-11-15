# 周期采集应用板端初测

用户加载 als-v1 后运行 sampling-v1，启动显示 interval_ms=1000、alarm=DISABLED。57次尝试均valid=1，最终 `EXIT state=STOPPED attempts=57 successes=57 errors=0 missed=0`，随后 `app_exit=0`。

首次样本elapsed_ms=4，之后大多为1001、2001、3001等，attempt43为42003；说明本次运行约1秒周期，但不据此宣称硬实时。正常光照主要77–81，第21–25次为0，第26–30次恢复78–80，第31–52次主要0，随后恢复70–79。符合用户按前述遮挡/移开步骤操作时的变化，不将原始值称为lux。

日志出现四次 `STATUSSAMPLE ...`，只有输入回显，未见 `STATUS state=RUNNING ...` 响应，不能记录STATUS已通过；需要确认换行送达与终端行规程。尚无板端失败恢复或周期遗漏实测；对应原生文件夹具和进程测试已通过，不能代替板端I2C故障验证。

后续用户执行 `(sleep 2; printf 'STATUS\n'; sleep 1) | ./light_monitor`，成功获得 `STATUS state=RUNNING valid=1 ALS_RAW=79 attempts=3 successes=3 errors=0 missed=0 elapsed_ms=2001 interval_ms=1000 last_errno=0 alarm=DISABLED`。共4次成功采样后EOF正常结束。`stty -a` 显示115200、icrnl、icanon、isig和echo启用，回车映射与规范行模式没有发现异常。命令解析与带换行输入链路已通过，手工按回车查询尚无新的原始输出，不需要据此修改代码。

板端解绑/重绑测试通过：前三次正常读取77–78；解除0-001e绑定后第4–6次均为 `valid=0 ALS_RAW=NA errno=2`，错误数依次1、2、3；STATUS显示attempts=6、successes=3、errors=3、valid=0、last_errno=2。重新bind并取得als-v1 ready后，第7次自动恢复79，后续78、77；STATUS显示attempts=9、successes=6、errors=3、valid=1、last_errno=0。EOF退出统计为attempts=10、successes=7、errors=3、missed=0。失败期间未返回旧数值，恢复后累计错误数保留。该结果是节点暂时缺失后的恢复，不是I2C物理通信故障实测。

## 周期遗漏及阶段验收

用户在板端SIGSTOP暂停应用约3秒后SIGCONT恢复，再SIGINT退出。样本相对时间依次为3、1001、2001、5046、6001、7001ms；第4次恢复时missed从0变为2，后续保持2，没有连续补出3000、4000ms两个遗漏周期。退出行 `EXIT state=STOPPED attempts=6 successes=6 errors=0 missed=2`，`app_exit=0`。

第06阶段验收覆盖默认周期、最新状态快照、STATUS命令、无效标记与错误统计、设备重新可用后的恢复、线程独立采集、遗漏计数不补采、Ctrl+C及EOF退出。命令边界、半条输入和长周期退出亦有原生进程测试。该范围已通过；真实I2C故障仍属于任务04的独立未完成项，CSV、报警及按键控制尚未加入本版本。
