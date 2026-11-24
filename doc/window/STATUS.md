# Window Mode 状态

## 已完成

- W0.1：计划、模块边界、状态文档已建立；
- W0.2：启动契约已确定。`window_entry(title, command_line)` 先创建 ConDrv `\Server`，建立进程内控制台会话和窗口，再启动 client；等价于 `conhost.exe cmd.exe`，不使用 ConPTY/defterm/libcorepty。
- W1：`corehostw` target、共享 `main.cpp`、`COREHOST_WINDOW` 宏切换已建立；defterm/comserver/headless 路径仍完整保留，仅普通 client 启动路径切换为 `window_entry`；
- `run_com_handoff`、`run_headless` 和 `com_handoff_bridge` 已删除，逻辑直接内联在 `wWinMain`；
- W2.1：`libcorewindow` 最小 top-level Win32 窗口和消息循环已建立；
- W2.3：`WM_DPICHANGED` 使用系统建议矩形调整窗口，`WM_GETMINMAXINFO` 设置最小尺寸；
- 公共 ConDrv `\Server` 创建辅助函数已添加；
- `window_client_process` 已通过 `PROC_THREAD_ATTRIBUTE_CONSOLE_REFERENCE` 和继承的 console 句柄启动 client；
- `window_entry` 当前顺序：server、console session、client、window；
- `libd3d12ui` 已建立 D3D12 device/queue、composition swapchain、DirectComposition visual、clear pass 和 resize；
- GPU fence 使用 `win32::event`，不暴露裸 `HANDLE`；
- 已加入 D3D12 文本 pass：运行时编译 VS/PS、根签名、PSO、R8 alpha 纹理上传和带混合的 quad；
- `window_host` 当前用 `CoreHost` 调试字符串验证文本纹理到窗口的完整路径；
- `libuitext` 已建立 header-only DirectWrite 文本度量与 UTF-16 grapheme cluster alpha 光栅化，使用 `libscreen::grapheme_range`；
- 字体 fallback 项由 `font_fallback_entry{family, range}` 组成，每个字体项只有一个 Unicode 范围，默认全范围；同一字体可重复出现并指定不同范围；
- 该列表交给 `IDWriteFontFallbackBuilder`，并追加 `IDWriteFactory2::GetSystemFontFallback()`；整个字符串直接交给 `IDWriteTextLayout`，由其完成 fallback、cluster 和 shaping，不手工切分字符串也不逐字符探测；
- glyph 输出使用可复用的 alpha/region 缓冲区，`glyph_bitmap` 只持有 span，不逐 glyph 分配 vector；
- `window_console_session` 已实现 CONNECT、CREATE_OBJECT、CLOSE_OBJECT、DISCONNECT、RAW_FLUSH、RAW_WRITE、RAW_READ、USER_DEFINED 的基础分派；
- 实测 `corehostw cmd.exe` 可以保持 client 运行，关闭窗口后 corehostw 与 cmd 均退出。

## 未完成

- W1：corehostw target 与 main 宏切换已建立，仍需入口行为测试；
- W2.1：最小 top-level 窗口已建立；
- W2.2 及之后：控制台 server 会话、进程启动、D3D12、输入/IME、渲染。

## 已知问题

- `CONSOLE_IO_RAW_WRITE` 当前只完成写入，不解析 VT，也不进入渲染；
- 文本 pass 目前只显示固定调试字符串，尚未绑定 `window_console_session` 的 screen buffer；
- 当前只上传第一个 glyph run，还没有 glyph atlas / 多 run 布局；
- `console_api` 的完整 API 覆盖仍待验证；
- 窗口关闭时直接终止 client，尚未实现完整的 console close 事件语义。

## 下一步

1. 从 `window_console_session` 暴露 screen buffer 快照；
2. 将 screen rows 交给 `IDWriteTextLayout`，按 cell metrics 放置；
3. 改为 glyph atlas / 多 run 绘制；
4. 接入光标、选择、颜色；
5. 完成关闭语义与自动化 smoke test。
