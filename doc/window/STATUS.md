# Window Mode 状态

## 已完成

- W0.1：计划、模块边界、状态文档已建立；
- W0.2：启动契约已确定。`window_entry(title, command_line)` 先创建 ConDrv `\Server`，建立进程内控制台会话和窗口，再启动 client；等价于 `conhost.exe cmd.exe`，不使用 ConPTY/defterm/libcorepty。
- W1：`corehostw` target、共享 `main.cpp`、`COREHOST_WINDOW` 宏切换已建立；defterm/comserver/headless 路径仍完整保留，仅普通 client 启动路径切换为 `window_entry`；
- `run_com_handoff`、`run_headless` 和 `com_handoff_bridge` 已删除，逻辑直接内联在 `wWinMain`；
- W2.1：`libcorewindow` 最小 top-level Win32 窗口和消息循环已建立；
- 公共 ConDrv `\Server` 创建辅助函数已添加；
- `window_client_process` 已通过 `PROC_THREAD_ATTRIBUTE_CONSOLE_REFERENCE` 和继承的 console 句柄启动 client；
- `window_entry` 当前顺序：server、console session、client、window；
- `libd3d12ui` 已建立 D3D12 device/queue、composition swapchain、DirectComposition visual、clear pass 和 resize；
- `libuitext` 已建立 header-only DirectWrite 文本度量与 UTF-16 grapheme cluster alpha 光栅化，使用 `libscreen::grapheme_range`，不引入 UTF-32 字符串或 UINT32 codepoint 容器；
- `window_console_session` 已实现 CONNECT、CREATE_OBJECT、CLOSE_OBJECT、DISCONNECT、RAW_FLUSH、RAW_WRITE、RAW_READ、USER_DEFINED 的基础分派；
- 实测 `corehostw cmd.exe` 可以保持 client 运行，关闭窗口后 corehostw 与 cmd 均退出。

## 未完成

- W1：corehostw target 与 main 宏切换已建立，仍需入口行为测试；
- W2.1：最小 top-level 窗口已建立；
- W2.2 及之后：控制台 server 会话、进程启动、D3D12、输入/IME、渲染。

## 已知问题

- `CONSOLE_IO_RAW_WRITE` 当前只完成写入，不解析 VT，也不进入渲染；
- 窗口目前由 D3D12 清屏，但没有文本渲染；
- `console_api` 的完整 API 覆盖仍待验证；
- 窗口关闭时直接终止 client，尚未实现完整的 console close 事件语义。

## 下一步

1. 将 `create_server_handle` 接入 window_entry；
2. 创建 reference/input/output 客户端句柄；
3. 启动控制台 server 会话；
4. 用 `PROC_THREAD_ATTRIBUTE_CONSOLE_REFERENCE` 启动 client；
5. 建立窗口与控制台会话的关闭/退出生命周期；
6. 添加自动化构建/smoke test。
