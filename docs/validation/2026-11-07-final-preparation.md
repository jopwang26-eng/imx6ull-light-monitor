# 最终验收准备及交付候选

2026-11-07，任务09进行中。未因主要功能打通而标记整机故障验收完成。

## 实际环境与完整重建

Ubuntu宿主uname为4.15.0-142-generic x86_64，编译器为Linaro GCC4.9.4；这不是板端内核uname。板端实际uname、挂载参数需由collect_board_env.sh补充。ens37仍为192.168.50.130/24；NFS导出/home/jopwang/linux/nfs，板IP既定192.168.50.131。根文件系统由NFS提供。

发现rootfs BusyBox为动态链接ARM EABI5，依赖/lib/ld-linux-armhf.so.3。因此不能假设断开NFS时还能从根文件系统启动Shell/sleep/tee等工具，静态应用也应从RAM运行，避免文件映射缺页依赖NFS。新nfs_observer静态程序在RAM保存观察记录并转发子应用输出；仅避免测试入口本身的可执行文件依赖，不证明所有路径查询都不依赖NFS。

实际工程新增tools最终脚本。build_release.sh --test重建三个模块、应用、工具和独立DTB，6采集+5日志+7控制进程测试全部通过；新观察器原生测试验证STATUS/STOP/START、SIGINT退出、RAM报告与输出一致，通过。脚本语法检查通过。NFS脚本只运行--check，未实际中断网络。

交付候选Ubuntu目录 `/tmp/light-monitor-release.N98PmX`，本地artifacts/light-monitor-candidate-2026-11-07.tar.gz；包SHA256为 `a5e20c7c6093267f1521e175d52d96cc7571d5ddc8fc97c19da6af17f0d9d75a`，Windows核对一致。59项清单验证通过。构建后核心app、三个ko及DTB与此前已验收部署逐项cmp一致，未改变业务代码或驱动行为。

已备份安装验收工具与候选文件，备份目录 `/home/jopwang/linux/nfs/rootfs/opt/light_monitor/backup_release_20261107_122428`。安装器初次未使用sudo时安全拒绝，随后交互sudo执行成功，没有依赖失败结果宣称部署完成。

## 工具及尚未执行的步骤

collect_board_env.sh只读采集板端内核、挂载、设备绑定及hash。test_buzzer_unavailable.sh临时解绑蜂鸣器，验证应用返回关闭错误、UNKNOWN、暂停及重绑恢复，预期有历史控制错误而退出1；它不制造物理引脚卡死。soak_board.sh默认正常存储连续60秒，保存控制台与日志，尚待板端执行。

NFS中断脚本限制在ens37上指定板IP到服务端的2049端口，先登记20秒独立恢复，再临时DROP12秒；不停止全局NFS服务、不清空防火墙、不影响SSH端口。正常结束移除指定测试跳转和链；若恢复校验失败则保留后备定时器。--run尚未执行，自动恢复方案尚未进行真实故障实测。

NFS实测须先准备专用tmpfs并启动RAM静态观察器；应用日志仍位于NFS。恢复规则不代表客户端RPC立即恢复，退出可能继续等待I/O；记录实际持续时间和报告，不承诺固定时间退出或零丢失。真实I2C硬件通信故障仍独立待验收，不能用设备解绑、原生错误夹具替代。

## 输出设备暂时不可用：待干净复测

用户提交的终端文本显示应用输出又作为Shell命令执行，出现READY/SAMPLE/CONTROL/STATUS/EXIT not found及带提示符路径的Permission denied。夹杂片段包含控制errno19后转2、STOP applied=1/PAUSED/UNKNOWN、重新绑定后OFF及START恢复，EXIT片段为15成功全部保存、control_errors=7、buzzer=OFF；这些片段方向符合测试预期，但本次文本未能可靠保留原脚本退出码（后续命令已覆盖）。不据此宣称完整故障测试通过。下一步将脚本stdout/stderr重定向到新建临时报告，立即打印退出码，再只读报告尾部，避免把输出再次执行。

## 板端环境与输出设备缺失：后续干净实测通过

用户重新提供完整无混入的脚本输出和紧随其后的test_exit=1，先前证据缺口已补足。

板端uname为 `Linux 192.168.50.131 4.1.15 #2 SMP PREEMPT Mon Oct 19 20:04:05 CST 2026 armv7l GNU/Linux`。根挂载来自192.168.50.130:/home/jopwang/linux/nfs/rootfs，NFSv3、hard、TCP、timeo=600、retrans=2、rsize/wsize=4096、nolock；/tmp为tmpfs。三个设备链接分别为lightkey-demo、lightbeep、lightap3216c。应用与三个模块SHA256与候选清单全部相同（app bafc115a…，key 6fd803fc…，AP 487b63d0…，beep fc2b1ca1…）。

输出设备暂时不可用测试：前8次采样有效，设100/200后raw0触发ON。解绑蜂鸣器后输出操作先errno19(ENODEV)、后errno2(ENOENT)，buzzer=UNKNOWN；STOP返回ERR control_failed applied=1、PAUSED、alarm=OFF、control_errno=2。STATUS保持采样统计和已保存记录，不伪装输出已关闭。

重新绑定beep-v1.1后，暂停重试恢复buzzer=OFF、control_errno=0、control_errors=7。START恢复采集及报警；EOF退出为15次成功全部保存、无采集/按键/日志错误或丢弃、control_errors=7、control_errno=0、buzzer=OFF，test_exit=1。故障报告、暂停关闭请求失败的可见性、恢复及保留历史错误、最终关闭均有板端日志。该结果是设备接口不可用，不宣称物理引脚卡死已测。

下一步60秒正常存储持续运行，再在RAM静态观察器下实测NFS端口中断。后者尚未执行，任务09及真实I2C硬件故障仍未完成。

## 持续运行正常存储测试通过

用户运行soak_board.sh 60，报告目录/opt/light_monitor/soak.gsjIVy。100ms周期运行约61秒，EXIT显示attempts=610、successes=610、errors=0、missed=0、log_saved=610、log_dropped=0、log_errors=0、log_failed=0、log_uncertain=0、control_errors=0、key_errors=0、buzzer=OFF；app_exit与soak_exit均为0。CSV为611行，含1行表头及610条记录。此为本次短时持续运行证据，不宣称长时间可靠性。

## NFS观察就绪协调更新

新增可选LM_NFS_READY_FILE：40秒观察期间在约34秒以临时文件+rename发布就绪标记，Ubuntu脚本预先等待该新标记（180秒超时不改网络），再中断NFS端口12秒，使约40秒的退出请求落在中断期间。先登记独立20秒恢复的机制保留。观察器原生测试新增完整标记发布校验，通过；脚本语法及ARM静态构建通过。新观察工具SHA256为27ab9e3d48f1780eb73fdf0ca21e08d31bcaaa2c2679f5e625c99f430b31d096，已更新到板端NFS运行目录。业务应用和驱动未改。

该协调更新发生在交付候选包生成之后；现有候选包保留原始版本及哈希，最终交付应重新打包最新测试工具和实际结果。当前仍未实际中断NFS，先等待用户运行prepare_nfs_board.sh提供新RAM目录与就绪路径。

## 第三阶段交接前检查：等待超时，未改变网络

用户执行prepare_nfs_board.sh生成RAM目录/tmp/light-monitor-ram.gC1JWC、日志目录/opt/light_monitor/nfs-observation.vlIizg，Ubuntu随后启动就绪等待器。用户尚未报告启动RAM观察器，转而要求将对话总结迁移到同项目“第三阶段”。

交接前通过SSH实查/tmp/light-monitor-nfs-vlIizg.log：先记录WAITING_FOR_BOARD，随后为`READY_TIMEOUT; no network change`；新标记不存在（READY_MARKER_ABSENT），等待进程已结束。脚本在进入创建恢复定时器和添加规则之前超时退出，本轮没有执行NFS中断，不能将准备成功写成故障验收通过。旧exec session 39861不再作为活跃会话续接。

下次先重新prepare生成新目录，再协调Ubuntu等待器与板端观察器。根HANDOFF.md已更新为第三阶段接续说明；主要功能保持通过，任务04真实I²C故障和任务09最终验收、最新工具重新打包仍未完成。

## 新一轮 NFS 观察：板端输出，主机证据待补

用户使用新目录 `/tmp/light-monitor-ram.LQ9b9D` 与 `/opt/light_monitor/nfs-observation.1Wv9CL` 运行静态观察器。板端输出显示约34.044秒发布就绪标记（`OBSERVER_ARMED error=0`）；35.208秒状态为169条已保存、队列3条、1条写入中，37.208秒为169条已保存、队列13条、1条写入中，39.208秒队列23条、1条写入中。此期间采样和STATUS仍有输出；20秒附近的STOP/START发生在就绪标记之前，不能据此声称中断期间暂停/恢复已测试。

观察器在40.059秒发送SIGINT；应用最终EXIT报告197次成功、197条保存、零遗漏/丢弃/日志错误，蜂鸣器OFF；观察器在48.174秒报告 `child_done=1 child_code=0`。这是该次输出所示结果，不承诺其他中断时长下零丢失或固定时间退出。用户尚未提供Ubuntu端 `NFS_DROP_BEGIN/END`、规则清理证据、板端Shell保存的观察器退出码及RAM报告；因此暂不能确认实际网络故障窗口和恢复清理，不能宣布NFS故障验收完成。Ubuntu普通用户只读检查时本轮目录权限不足，`sudo -n`需要密码，未据此推断规则状态；未发现仍存活的等待脚本进程。

后续用户提供Ubuntu交互sudo只读检查：`iptables -S INPUT`仅输出 `-P INPUT ACCEPT`，`iptables -S LM_LIGHT_NFS_TEST`输出 `No chain/target/match by that name.`。据此确认检查时测试跳转及测试链均无残留。实际中断起止仍待原终端的 `NFS_DROP_BEGIN/END` 输出；不为补证据重新执行中断脚本。

## 本轮实际中断证据补齐

用户补充原Ubuntu终端输出：恢复定时器light-monitor-nfs-restore-41392已登记，`NFS_DROP_BEGIN 2026-11-07T05:05:12Z`、`NFS_DROP_END 2026-11-07T05:05:24Z`，对应北京时间13:05:12至13:05:24，共12秒。结合前述板端积压后恢复、197条全部报告保存、child_done=1/child_code=0及规则无残留，本轮NFS端口中断观察已完成归档。完整结论及未覆盖范围见[专项记录](2026-11-07-nfs-board.md)。此前“起止待补”为当时状态，现已补齐；Shell观察器退出码、RAM报告与CSV独立核对未另行取得，不据此添加已执行声明。

## 含实测证据的更新候选包

Ubuntu正式工程备份并更新打包脚本及包README，将板端validation文档纳入包内和哈希清单。实际运行 `sh tools/build_release.sh --test` 重建成功：6采集、5日志、7控制共18项测试及观察器回归全部通过；81项清单校验通过。新包 `/tmp/light-monitor-release.8GD7fh.tar.gz` 已复制到Windows `artifacts/light-monitor-candidate-2026-11-07-nfs-verified.tar.gz`，两端SHA256均为 `07d783668b2c3e0acee3c0efaad2d23b0b649b11873f4acbbbba24cf70c7741e`。

包内应用、三个模块、测试DTB和观察器SHA256逐项与此前已板测版本一致，应用和观察器另与NFS部署文件cmp一致。新包含最新就绪握手工具及本轮NFS实测记录；旧候选包保持原文件和原哈希。未重新安装、未再中断网络。任务09在既定验证范围内完成，真实I²C硬件通信故障仍作为任务04保留未验收，不能宣布整个项目全部验收完成。

## 最后一项通信错误验收完成

用户随后提交独立NACK测试完整输出：只读0x1f实际传输返回-5，同fd字符read=-1/errno5且缓冲区哨兵不变，关闭注入后恢复read=2。应用36尝试=26成功+10错误，26条全部报告保存，故障NA/报警OFF/蜂鸣器OFF，恢复有效/报警ON，最终OFF。正式模块恢复probe及3次重读、正式ko哈希校验通过，TEST_EXIT与script_exit均0。见[最终I²C专项记录](2026-11-07-i2c-nack-board.md)。任务04阶段验收完成；物理断线/断电未实测仍是已知边界。上述NFS候选包早于本轮最后证据；最终归档需纳入本轮记录和独立测试附件。

## 最终归档完成

本轮从Ubuntu正式工程完成正式产物重建，18项应用测试及观察器回归通过。独立NACK附件携带本次已板测的原模块与静态检查程序，源码同时收录；普通安装器不会安装test-only目录。95项完整性清单通过，正式应用、三模块、DTB、观察器及测试附件哈希与本轮已验证指纹一致，测试base.c与正式驱动源cmp一致。

Ubuntu `/tmp/light-monitor-release.9MTSUI.tar.gz` 已复制到Windows `artifacts/light-monitor-release-2026-11-07.tar.gz`，两端SHA256为 `db1dbf09d54bf534c74b59f429deeaf115d93ab02cf3a84df42e7ac78d8adbf5`。任务01至09在记录的范围内全部完成；旧两版候选包保留。本段为归档后本地索引，包内已包含最后板端证据及正式完成范围说明。
