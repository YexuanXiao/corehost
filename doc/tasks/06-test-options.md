# 测试选项与运行方式

本文说明 corehost 测试的默认行为、可配置选项和常用运行方式。所有测试都应通过
`ctest` 运行（构建目录是 Visual Studio 多配置生成器，必须带 `-C`）。

## 1. 默认运行

```powershell
cmake -B build
cmake --build build
ctest --test-dir build -C Debug --output-on-failure
```

默认行为（2026-11 起）：

- 26 个测试目标，全量约 **33s**；
- **不与系统 conhost 比较**：`ConPTY.Differential` 默认只跑 corehost 侧冒烟，
  约 0.2s；
- **不启动真实 Windows Terminal / 真实默认终端委托**：相关机会式用例已删除；
- **不弹出可见窗口**：ConPTY 宿主都用 `--headless`；测试子进程在无控制台父进程
  下用 `CREATE_NO_WINDOW`；ssh 非交互远程命令运行在隐藏桌面上（见
  `design/0003`）；
- 依赖环境缺失时用例 SKIP，而不是失败（ssh/sshd 不存在、系统 conhost 缺失等）。

## 2. 环境变量

| 变量 | 默认 | 作用 |
| --- | --- | --- |
| `COREHOST_TEST_DIFFERENTIAL` | 未设置 | 设置后 `ConPTY.Differential` 与系统 `conhost.exe` 做差分比较（api 场景），约 0.3s；需要 `%SystemRoot%\System32\conhost.exe` |
| `COREHOST_TEST_DIFFERENTIAL_CTRL` | 未设置 | 仅在同时设置 `COREHOST_TEST_DIFFERENTIAL` 时生效：额外记录 raw/record 读遇 Ctrl+C 的行为（**只记录不比较**）。会多等 conhost 的两个 ~3s 超时，总耗时约 +8s |
| `COREHOST_TEST_SHELL` | 自动查找 powershell/pwsh | `ConPTY.PowerShellError` 使用的 shell 路径 |
| `COREHOST_DISABLE_LOG` | 构建选项，默认 ON | `-DCOREHOST_DISABLE_LOG=OFF` 打开日志 |
| `COREHOST_LOG_LEVEL` | 构建选项，默认 1 | 日志级别 1/2/3（1=低频，3=高频）；例如 `-DCOREHOST_LOG_LEVEL=2` |

> 差分比较没有默认开启：它会复制系统 conhost、运行两套宿主，且 raw/record
> Ctrl+C 场景在 conhost 下不会返回，只适合手动排查兼容性。

## 3. 常用命令

```powershell
# 只跑某一组
ctest --test-dir build -C Debug -R ConPTY
ctest --test-dir build -C Debug -R Defterm
ctest --test-dir build -C Debug -L conpty

# 打开系统 conhost 差分（默认关闭）
$env:COREHOST_TEST_DIFFERENTIAL = '1'
ctest --test-dir build -C Debug -R ConPTY.Differential
Remove-Item Env:COREHOST_TEST_DIFFERENTIAL

# 连同 raw/record Ctrl+C 记录一起跑（慢）
$env:COREHOST_TEST_DIFFERENTIAL = '1'
$env:COREHOST_TEST_DIFFERENTIAL_CTRL = '1'
ctest --test-dir build -C Debug -R ConPTY.Differential

# 直接跑单个隔离用例（runner 支持 --case-index，索引见用例表顺序）
build\Debug\test_conpty_programs.exe --case-index 4
```

## 4. 测试分组与大致耗时（Debug、参考值）

| 目标 | 内容 | 约耗时 |
| --- | --- | --- |
| `Defterm.Connect` | 直连 ConDrv 的 defterm 路径：控制台 API、伪窗口、压力、5 次重复、进程列表 | 0.9s |
| `Defterm.Handoff` | handoff 协议替身（COM 边界 mock） | 0.03s |
| `ConPTY.Differential` | 默认 corehost 冒烟；opt-in 时与 conhost 比较 | 0.2s（默认）/0.3s（比较）/8s（含 Ctrl 记录） |
| `ConPTY.Programs` | cmd / 脚本 / powershell / vim / edit / ssh 正负路径 / 窗口可见性 | 9s |
| `ConPTY.Robustness` | 19 个会话/信号/输入/窗口用例 | 10s |
| `ConPTY.PowerShellError` | PowerShell 报错换行/颜色 | 3.3s |
| 其余 | 单元测试、ConPTY 新行/键盘/光标/E2E、CLI、平台自测等 | 合计约 10s |

## 5. 日志与失败产物

- 宿主日志：`build/Debug/logs/`（受 `COREHOST_DISABLE_LOG`/`COREHOST_LOG_LEVEL` 控制）；
- 失败用例产物：`build/Debug/test-artifacts/<case>/`；
- 差分目录：`build/Debug/differential/{corehost,conhost}/`（仅 comparison 模式会
  复制系统 conhost）。

## 6. 注意

- `ctest` 必须带 `-C Debug`；不带时会显示 Not Run。
- 测试不应修改注册表、服务状态或用户默认终端配置。
- 如果再次看到可见窗口，用下面的命令定位是哪个进程：
  ```powershell
  Get-Process | Where-Object { $_.MainWindowHandle -ne 0 } |
      Select-Object Id, ProcessName, MainWindowTitle
  ```
  并检查是否新增了未经 `CREATE_NO_WINDOW`/隐藏桌面/`--headless` 处理的子进程。
