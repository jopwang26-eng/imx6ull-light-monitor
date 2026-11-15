# control-v1 板端联调

用户经boot_lightbeep.ttl启动后执行start_light_monitor.sh，输出GPIO_LOGICAL=0及READY control-v1。默认1000ms周期、阈值未启用；前13次读数有效，约3–7，missed=0、alarm=DISABLED。

STOP提交后返回OK、applied=1、state=PAUSED、buzzer=OFF、control_errno=0。随后STATUS为valid=0、ALS_RAW=NA、attempts=13、successes=13、errors=0、missed=0、log_saved=13、log_dropped=0、log_errors=0，key_enabled=1、key_errno=0、key_errors=0、control_errors=0。暂停期间没有新的采样输出。

START提交后返回RUNNING，第14次约38469ms恢复采样，之后约每1000ms继续，missed仍为0，暂停时间不算遗漏。第26次后再次STOP成功，当前应用保持PAUSED并确认buzzer=OFF。

输入回显与SAMPLE偶尔拼在同一显示行，但完整换行提交后均有正确OK响应；不将回显交错当作协议解析失败。STOP/START/STATUS正常路径已验证，运行时SET_INTERVAL、KEY0实际切换、阈值/声音/CSV及故障关闭和退出等尚待后续板测。

KEY0整合板测通过：两次 `lightkey: debounced press` 后分别输出 `KEY action=TOGGLE state=RUNNING control_errno=0` 和PAUSED，恢复后产生第27–34次样本，再次按下后采样停止。STATUS为PAUSED、valid=0、ALS_RAW=NA、attempts=34、successes=34、errors=0、missed=0、buzzer=OFF、control_errors=0、key_errors=0、log_saved=34、log_dropped=0、log_errors=0。

期间STASTUS、STATU及含控制字符的错误输入均返回ERR unknown_command，之后正确STATUS正常响应，状态未意外变化。退格回显为^H，未对终端编辑设置作修改。当前仍PAUSED，运行时周期修改及阈值联调待继续。

新一次应用运行中SET_INTERVAL 500返回OK、RUNNING、interval_ms=500；第6次样本4972ms，随后5472、5972、6472等直至第38次20972ms，间隔500ms，missed=0。STOP后STATUS显示PAUSED、valid=0、attempts=38、successes=38、errors=0、interval_ms=500、buzzer=OFF、control_errors=0、key_errors=0、log_saved=38、log_dropped=0、log_errors=0。运行中周期修改已板测通过。当前应用仍运行于PAUSED，阈值未设置。

阈值联调：SET_THRESHOLD 2 4成功后START，500ms周期第39–64次主要3–7，alarm=OFF，其中第50次raw=2仍OFF；第65次raw=1触发ON、第66–68次raw=0保持ON，第69次raw=5解除为OFF。随后正常光照仍OFF。最后STOP时报警原已OFF，STATUS显示PAUSED、valid=0、threshold_enabled=1、low=2、high=4、attempts=91、successes=91、errors=0、missed=0、buzzer=OFF、control_errors=0、key_errors=0、log_saved=91、log_dropped=0、log_errors=0。

已验证低于下限触发、高于上限解除，以及解除状态下恰等于下限2/处于区间3时保持；开启状态下等号与区间保持仅有原生测试证据。用户尚未明确反馈本次各阶段声音。STOP在报警已解除时执行，不能代替报警持续ON时的STOP关闭和退出关闭，后两项待继续。

随后安排保持遮挡直到alarm=ON并发声后STOP/STATUS，再START恢复报警后Ctrl+C退出，并查看退出码与CSV尾部；用户回复“均成功”。据此记录本次实物发声、STOP停止声音及Ctrl+C停止声音为用户成功反馈，未追加该组原始STATUS、EXIT、退出码和CSV内容，不编造具体计数或退出码值。传感器暂时不可用时的报警关闭仍需在control-v1上补充验证。

用户补充退出后 `app_exit=0` 以及CSV尾部：第154–155条为raw7/OFF，第156–161条为raw0/ON，第162–163条仍为raw0/ON，序号连续。确认业务报警判断已写入CSV，且退出码0。最后记录保持ON属于退出前成功采样的历史状态；退出关闭不形成新的采样记录，不能将最后CSV行当作退出后的实物输出。EXIT原始行仍未提供。

## 传感器暂时不可用时的关闭与恢复

新进程使用500ms周期，命令设置阈值100/200，当前raw0使第3–7次alarm=ON。解绑AP3216C后第8–11次均 `valid=0 ALS_RAW=NA errno=2`，STATUS为attempts=11、successes=7、errors=4、alarm=OFF、buzzer=OFF、control_errno=0、control_errors=0、log_saved=7。

重新绑定出现als-v1 ready；第12次恢复raw0及alarm=ON，后续STATUS为attempts=15、successes=11、errors=4、valid=1、last_errno=0、alarm=ON、buzzer=ON。EOF退出原始行为：`EXIT state=STOPPED attempts=17 successes=13 errors=4 missed=0 log_saved=13 log_dropped=0 log_errors=0 log_failed=0 log_errno=0 log_uncertain=0 control_errors=0 control_errno=0 buzzer=OFF key_errors=0 superseded=0`；随后 `app_exit=0`。

确认失败读数不作为低光照触发报警，临时缺失后关闭输出并自动恢复；传感器错误统计与输出控制错误分开。最新测试有GPIO回读及状态日志，未新增独立声音文字反馈；此前报警及STOP/退出声音已获用户明确成功反馈。此处仍不是I2C物理通信故障注入。

## 第08阶段结论

统一控制与报警整合阶段验收完成。板端覆盖五条命令正常路径、KEY0启停、500ms周期调整、阈值触发/解除、CSV报警字段、暂停与退出关闭、设备缺失后的故障关闭与恢复；原生进程测试补充参数边界与非法值、长行/半行、滞回完整等号边界、关闭EIO报告和在途读取STOP竞态。不同证据类型分别保留。真实I2C故障、GPIO物理关闭失败及NFS中断/阻塞仍未完成，继续作为任务04/09的独立事项。
