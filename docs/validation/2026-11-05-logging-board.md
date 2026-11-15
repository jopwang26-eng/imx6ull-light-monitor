# 异步CSV日志板端验收

用户在板端运行logging-v1默认1000ms周期和10MiB上限，3秒时发送STATUS、再2秒EOF退出。共6次成功读取，值依次78、77、75、77、75、75。

STATUS时successes=4、log_saved=3、log_inflight=1、log_queue=0、log_dropped=0、log_errors=0，表明有1条在途记录，非丢失。EXIT显示attempts=6、successes=6、errors=0、missed=0、log_saved=6、log_dropped=0、log_errors=0、log_failed=0、log_errno=0、log_uncertain=0，app_exit=0。

`logs/light_monitor.csv`大小145字节，表头为sequence,elapsed_ms,als_raw,alarm，后含上述6条样本，序号1–6，相对时间4、1001、2001、3001、4001、5001ms，报警列DISABLED。正常保存和退出排空已板测通过。

板端轮换、重启追加、存储错误恢复仍待验证；慢存储队列满和真实短写错误已在原生进程测试中通过，不宣称真实NFS故障已实测。

板端执行100ms周期、独立logs_rotation目录、128字节单文件上限，共31次成功采集；EXIT log_saved=31、log_dropped=0、log_errors=0、log_failed=0、log_uncertain=0，app_exit=0。目录中正好5个CSV：当前文件54字节，.1–.4各114字节，均未超过128字节。

完整内容显示当前文件序号31；.1为27–30、.2为23–26、.3为19–22、.4为15–18，每份均有正确表头、字段及完整行。轮换顺序、五文件限制和大小限制已板测通过；1–14为已保存后按保留策略淘汰的历史，不计为写入丢弃。重启追加与存储错误恢复尚待板测。

板端以临时目录缺失父目录触发存储打开错误，通过200ms周期继续采集。首次STATUS：attempts=6、successes=6、传感器errors=0、log_saved=0、log_dropped=6、log_errors=1、log_failed=1、log_errno=2。创建父目录后仍按约5秒重试，5001ms恢复，LOG显示failed=0、errno=0、errors=1、dropped=25。

后续STATUS显示36次成功中已保存11、丢弃25，累计存储错误仍为1。最终EXIT为attempts=41、successes=41、传感器errors=0、missed=0、log_saved=16、log_dropped=25、log_errors=1、log_failed=0、log_errno=0、log_uncertain=0；app_exit=1，按设计报告历史丢弃。CSV正好包含序号26–41共16条，故障期间的1–25没有伪造或补写。存储目录错误下采集/查询不中断、5秒重试、新记录恢复、退出统计均板测通过。该测试不是NFS连接中断或磁盘满实测。

剩余板端验收：重启追加保留既有日志；队列满及真实短写回退已有原生进程测试，不扩展为任意NFS故障已通过。

重启追加板测通过：运行前默认logs/light_monitor.csv有一份表头和6条记录；再次运行1秒，EXIT显示successes=2、log_saved=2、log_dropped=0、log_errors=0、log_failed=0、log_uncertain=0，app_exit=0。运行后原6条逐行保留，末尾追加本次序号1、2（时间4、1001ms，原始值75、74），表头仍只有一份。

## 阶段结论

任务07验收完成：板端验证正常CSV、退出排空、五文件轮换与大小限制、目录故障时采集查询继续、5秒重试恢复、丢弃与退出失败报告、重启追加；原生真实进程测试验证64条队列满丢新记录和文件短写/EFBIG回退。证据类型分别保留，真实NFS断连/阻塞和掉电可靠性未实测，留任务09。
