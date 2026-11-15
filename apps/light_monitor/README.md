# 光照采集、统一控制及异步日志 control-v1

公开整理说明：本页保留实现和历史构建步骤；文末“未验收/须联调”等为对应阶段当时状态。后续NFS中断与I²C地址NACK等验收已完成，当前范围见[验证汇总](../../docs/VALIDATION.md)，公开目录的构建命令见[构建说明](../../docs/BUILD.md)。历史Ubuntu绝对路径不代表读者当前环境。

应用在独立采集线程中读取 `/dev/lightap3216c`，默认1000ms启动即采集；主线程poll串口及KEY0，五条命令与按键调用统一业务入口。独立日志线程保存成功样本为CSV。Ctrl+C、SIGTERM或stdin EOF请求正常退出，停止采集、关闭蜂鸣器并排空日志。本版本加入双阈值报警，不实现TCP。

## 运行

Ubuntu实际应用源码目录为 `/home/jopwang/linux/IMX6ULL/Drivers/Linux_Drivers/light_monitor/app`，不与上层内核模块Makefile混用。Windows副本位于本目录，二者不自动同步。

在Ubuntu运行 `make native` 和 `python3 tests/test_process.py ./light_monitor_native` 可验证应用进程。ARM构建使用既有工具链前缀，通过 `make CROSS_COMPILE=<前缀> LDFLAGS=-static` 生成静态程序；`sh deploy_sampling.sh` 备份同名程序并通过交互sudo部署到NFS。

板端进入项目目录，确认没有正在运行的旧测试读取工具，再执行：

```sh
cd /opt/light_monitor
sh ./start_light_monitor.sh
```

启动脚本沿用现有蜂鸣器测试DTB，加载缺少的三个驱动，检查字符节点并确认蜂鸣器可以关闭，再exec应用；不修改DTB和永久启动环境。若需单独测试设备缺失后的应用恢复，可直接运行 `./light_monitor`。退出后释放按键和蜂鸣器的独占fd；应用运行时不要另起测试工具竞争这两个节点。

每秒可见类似 `SAMPLE attempt=1 elapsed_ms=0 valid=1 ALS_RAW=86 missed=0`。输入 `STATUS` 回车，返回运行状态、有效性、最新原始值、尝试数、成功数、错误数、遗漏周期、最近一次尝试的相对启动时间、配置周期与最近错误码。无有效值时输出 `ALS_RAW=NA`，从不把旧值当作当前读数。首次尝试完成前valid=0、last_errno=0、attempts=0表示尚无读数。

启动参数 `-d DEVICE` 指定传感器，`-b DEVICE`指定蜂鸣器，`-k DEVICE`指定按键（`-k none`仅禁用按键入口），默认分别为 /dev/lightap3216c、/dev/lightbeep、/dev/lightkey。`-i MS` 选择100–60000ms周期，`-l DIR` 选择日志目录（默认当前目录的logs）。`-r BYTES` 将单文件上限设置为128–10485760字节，主要用于小规模轮换验证，默认10MiB。命令最多127字节（不含换行），超长或包含NUL时丢弃整行，不会把截断内容当作有效命令。

## 命令与按键

命令大写，空格或制表符分隔，回车提交（终端icrnl将CR映射为LF）。参数必须是无符号十进制数字，拒绝符号、溢出、尾随字符及多余参数。

| 命令 | 行为 |
|---|---|
| STATUS | 查询RUNNING/PAUSED、有效读数及采集、输出、按键、日志统计 |
| START | 开始/恢复采集；已运行时保持当前节奏 |
| STOP | 暂停采集并立即尝试关闭蜂鸣器，保留进程和控制入口 |
| SET_INTERVAL MS | 范围100–60000；运行中重设截止点并立即进行一次新采样 |
| SET_THRESHOLD LOW HIGH | 0≤LOW<HIGH≤65535；启用阈值并先关闭报警，重新取得样本后判断 |

合法修改返回OK；非法命令或参数返回ERR且不修改状态。关闭失败时返回 `ERR control_failed ... applied=1 ...`，说明暂停/阈值配置已应用，但蜂鸣器输出未确认。停止清理已开始时新请求被拒绝，applied=0。暂停期间STATUS显示valid=0、ALS_RAW=NA，累计统计和上一采样时间保留；暂停本身不计周期遗漏。

KEY0每次成功read取得二进制1才切换RUNNING/PAUSED。poll只报告就绪，不消费事件；真实驱动允许合并未消费的多次按下。KEY与命令共用lm_sampler_control，串口输入等待不阻挡按键。驱动保留的旧待处理事件可能在应用打开后被消费，也会产生一次切换。按键打开/读取失败单独报告并约1秒重试，串口入口继续可用。

## 报警与输出

每次进程重新启动恢复默认周期和未设置阈值状态，不持久化配置。未设置阈值时alarm=DISABLED，并尝试保持蜂鸣器关闭。设置后：原始值严格低于LOW时ON，严格高于HIGH时OFF；中间及等于任一边界时保持上次判断。重新设置阈值从OFF开始，故障/暂停关闭后恢复到中间区间也保持OFF。

alarm字段是业务判断；buzzer=ON/OFF只表示最近GPIO逻辑回读确认，UNKNOWN表示控制失败，不等同于声音检测。STATUS包含control_errno及累计control_errors。暂停、传感器读取失败、退出均尝试关闭，失败不伪装成功；暂停期间每秒重试关闭，运行期间每周期重试输出。每次输出失败都会释放旧蜂鸣器fd以支持重新绑定恢复；健康状态持有独占fd，避免其他程序抢控。

I2C读取在业务锁外，命令不必等待I2C结果。STOP/重新开始/配置变化使在途旧读数作废（superseded计数），防止旧结果在STOP返回后重新触发报警；作废结果不计新的有效采样或CSV记录。蜂鸣器操作由同一业务锁串行化；它可能让命令/状态查询等待GPIO操作完成，不承诺任意设备故障下立即响应。

## 异步CSV

CSV表头为 `sequence,elapsed_ms,als_raw,alarm`，只记录成功采样。sequence是本次进程的成功样本序号，elapsed_ms是相对本次启动的毫秒数，重启后重新开始；因丢弃可出现序号缺口。alarm列为DISABLED/ON/OFF，是该样本对应的报警判断而非物理声音。STOP之前已形成的记录仍可在暂停后被日志线程写完。

队列容量64，满时保留已入队记录，丢弃新记录，不等待存储恢复。日志线程独占文件I/O。当前文件为 `light_monitor.csv`，历史 `.1` 到 `.4`，.1最近，最多五个CSV。达到上限前轮换并覆盖最老历史，重启追加当前文件，不清空已有内容；目录中另有建议锁文件 `.light_monitor.lock`，防止本应用多个进程同时写同一目录。

每行完整write及fdatasync成功后才增加log_saved；这不是任意NFS故障或掉电零丢失保证。计数为本次运行累计，轮换淘汰的历史仍计入已保存。STATUS另外提供log_dropped、log_errors、log_queue、log_inflight、log_failed、log_errno、log_uncertain。快照与队列统计分两次短临界区获取，运行中不要求跨线程瞬间完全对齐；退出排空后可核对successes=log_saved+log_dropped。

存储失败后丢弃失败记录及待写队列，新样本仍采集但作为丢弃计数；日志线程约5秒重试，成功后新记录恢复写入，历史丢弃不会补写。失败行尽量截断回写前长度；若回退或同步也失败，增加uncertain并拒绝继续写同一inode，需保留现场、修复或换新日志文件。重启发现错误表头或未结束尾行时拒绝追加，防止静默混入损坏文件。

退出先停止采集并关闭输出，再排空日志。存在丢弃、退出时日志失败/不确定写入，或累计蜂鸣器控制/按键错误时返回1；历史错误恢复后仍保留累计统计。EXIT打印最终buzzer状态和control_errno，不能仅凭进程退出就认为实际声音已停止。日志错误输出LOG行，控制错误输出CONTROL，详细累计统计在STATUS与EXIT。故障时不为了等重试额外睡5秒，但在途NFS write/fdatasync仍可能阻塞，join不能保证立即完成，需系统阶段另测。

## 模块与同步

- `sensor.c`：每周期open/read/close，按两字节小端解码，短帧EIO；失败不修改调用者的有效输出。每次重新打开便于解绑重绑后自动恢复，不长期占用旧fd。
- `sampler.c`：独立线程、单调时钟与绝对截止时间；统一状态控制、滞回与蜂鸣器同步。I2C读取在锁外，快照在锁内发布，条件变量唤醒周期或暂停等待。
- `command.c`：解析命令、校验参数并调用业务入口，不依赖串口传输细节。
- `main.c`：poll等待stdin和按键，按完整行交给命令层，按有效事件调用TOGGLE；统一终端输出。信号只交给主线程，处理器仅置标志。
- `buzzer.c`：独占打开、写0/1并校验逻辑回读，错误关闭旧fd并保留错误码。
- `logger.c` / `logger.h`：64条有界队列、存储线程、轮换与失败重试。队列锁不覆盖存储I/O，避免存储阻塞直接阻挡采集和查询。

控制台打印最新快照，若终端阻塞期间发生多个采样，可能合并展示；CSV则独立消费采集记录队列。间隔不保证硬实时。设备read仍可能受内核I2C处理时间影响，退出会等待在途读取结束，不承诺任意故障下立即退出。stdout阻塞也可能延迟控制主线程。上述限制不能由用户态线程数量消除。

## 验证边界

原生测试使用普通文件提供协议帧，通过删除/原子替换产生缺失设备、短帧及恢复；用真实SIGSTOP/SIGCONT模拟应用调度暂停验证遗漏统计。它们验证应用行为，不能代替实际I2C通信故障、板端周期、串口操作和Ctrl+C实测。传感器任务04的真实I2C故障未完成验收仍独立保留。

日志测试 `tests/test_logging.py` 运行真实程序，检查CSV、重启追加、128字节小规模轮换、缺失目录恢复与5秒重试，并用RLIMIT_FSIZE触发真实短写/EFBIG验证回退。`tests/fsync_gate.c`仅在原生测试以LD_PRELOAD延迟真实fdatasync，让64条队列填满以验证丢弃新记录；它不编入ARM程序，不能视为真实NFS故障实测。

控制测试 `tests/test_control.py` 使用文件协议帧、蜂鸣器文件夹具和按键FIFO，验证命令、滞回两边界、统一切换、故障关闭及重启默认值；`control_faults.c`仅在原生LD_PRELOAD中受控返回关闭EIO或阻塞在途读取，验证应用错误传播与STOP竞态。它不编入ARM程序，不等同于GPIO物理关闭失败或真实I2C故障。control-v1仍须板端联调。
