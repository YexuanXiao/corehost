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
- 已加入 D3D12 文本 pass：构建期用 DXC 预编译 VS/PS（Shader Model 6.0 / DXIL） 并嵌入字节数组，运行期创建根签名、PSO、上传 R8 alpha 纹理并绘制混合 quad；
- 默认字体 fallback 列表为 `Cascadia Code`、`Cascadia Mono`、`Consolas`，与 Windows Terminal 的字体选择方向一致；
- `window_host` 已用 glyph atlas + per-cell quad 显示真实控制台内容；120x30 网格、Cascadia Code 16px；
- `libuitext` 已建立 header-only DirectWrite 文本度量与完整字符串 layout/alpha 光栅化；
- 字体 fallback 项由 `font_fallback_entry{family, range}` 组成，每个字体项只有一个 Unicode 范围，默认全范围；同一字体可重复出现并指定不同范围；
- 该列表交给 `IDWriteFontFallbackBuilder`，并追加 `IDWriteFactory2::GetSystemFontFallback()`；整个字符串直接交给 `IDWriteTextLayout`，由其完成 fallback、cluster 和 shaping，不手工切分字符串也不逐字符探测；
- glyph 输出使用可复用的 alpha/region 缓冲区，`glyph_bitmap` 只持有 span，不逐 glyph 分配 vector；
- `window_console_session` 已实现 CONNECT、CREATE_OBJECT、CLOSE_OBJECT、DISCONNECT、RAW_FLUSH、RAW_WRITE、RAW_READ、USER_DEFINED 的基础分派；
- 共享 `console` 只保存 screen/state/input；`win32::srwlock console_mutex` 独立于状态对象，由 `window_entry` 持有并同时传给 IO 会话和 UI，类似 libcorepty 的 `session_mutex`；IO 线程持写锁处理 console API，UI 线程渲染前主动获取同一把锁复制 damage 后立即释放；IO 线程不通知 UI、不使用回调、`std::mutex`、缓冲队列或 HWND 原子量；damage snapshot 直接使用 `screen::row_block`，damage 范围写在 `row_state.dirty_begin/dirty_end`；每个 active/history row 有自己的 `screen::grapheme_store`，cell offset 只在 row 内解释；渲染线程把损坏 cell 的 cluster 重映射到自己的 grapheme store；
- 已决定渲染路径：glyph atlas + per-cell quads，UI 线程构建/更新 atlas 和 quad 数据，不执行 console IO；
- 实测 `corehostw cmd.exe` 可以保持 client 运行，关闭窗口后 corehostw 与 cmd 均退出。

## 未完成

- W1：corehostw target 与 main 宏切换已建立，仍需入口行为测试；
- W2.1：最小 top-level 窗口已建立；
- W2.2 及之后：控制台 server 会话、进程启动、D3D12、输入/IME、渲染。

## 已知问题

- `CONSOLE_IO_RAW_WRITE` 当前只完成写入，不解析 VT，也不进入渲染；
- damage snapshot 已转成 per-cell quad；已按 cursor_state 的 visible/size 显示前景色块状光标；已定义可注入的 console_palette，默认 Campbell，实现 cell 前景/背景色；尚无选择、滚动条；
- 渲染线程按合成帧时钟唤醒，即使无 damage 也会被唤醒；尚未实现完全 idle 阻塞策略；
- 独立渲染线程已接入：DCompositionWaitForCompositorClock 驱动，damage 时取 screen 并 Present，UI 线程不渲染；
- 键盘输入已接入 WM_CHAR -> shared input queue；独立 input thread 等待 input event 并完成 pending read，不使用 CancelSynchronousIo；实测 echo 输入可自动显示；
- `console_api` 的完整 API 覆盖仍待验证；
- 窗口关闭时发送 CTRL_CLOSE_EVENT，期间保持 console server 存活，并无限等待 client 退出；client 析构不再 TerminateProcess。
- client 自行退出由独立监控线程检测；默认 `keep_open` 并在标题显示 `(client exited)`，C++ API 可选择 `exit_if_never_read_input` / `always_exit`。

## 下一步

1. 实现 530ms 光标闪烁，失焦时停止闪烁并隐藏；
2. 将 screen rows 交给 `IDWriteTextLayout`，按 cell metrics 放置；
3. 接入光标、选择、颜色；
4. 增加输入路径；
5. 完成关闭语义与自动化 smoke test。
