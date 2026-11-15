# 公开仓库文件范围

本仓库发布驱动与应用源码、按模块组织的Makefile、独立测试DTS、测试/启动/验收脚本和必要文档。原项目本地副本与压缩归档继续保留。

以下内容不提交：Codex及其他代理配置、API密钥、令牌、环境文件、SSH私钥、云凭据、机器本地配置、聊天交接、内部任务、个人简历和教学页面、原始图纸/截图、临时文件、运行日志、编译产物及压缩包。顶层gitignore采用明确目录允许清单，并继续排除各目录中的凭据与产物。

提交前在PowerShell执行：

```powershell
powershell -ExecutionPolicy Bypass -File tools/check_publication.ps1
git diff --cached --stat
git diff --cached --name-only
```

检查器只扫描Git暂存区中的实际内容，不输出命中的秘密值；发现可疑凭据或禁止路径时返回非零。模式扫描不能识别所有形式的秘密，新增文件仍须人工审查；不使用git add -f绕过忽略规则。

历史脚本中的实验网络私有IP与Ubuntu路径是环境示例，不是认证信息，移植前应调整。本地审查提交使用GitHub noreply邮箱；GitHub连接器生成的提交作者信息以平台账号配置为准，不能保证所有远端提交均使用noreply。源码中不加入个人联系信息。原厂BSP和第三方依赖不重新打包，也不替它们另行声明许可。
