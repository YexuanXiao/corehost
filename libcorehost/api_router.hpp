// ── conpty/api_router.hpp ──────────────────────────
// Layer 2: Console API 分派。
//
// 功能分解：
// 1. handle_user_defined 按 ApiNumber 高 8 位选择 L1/L2/L3 分发表。
// 2. active_screen_buffer 根据 alt_active 在主/备用缓冲区之间切换。
// 3. switch_active_screen_buffer 发送 VT alternate-buffer 序列，并把当前
//    screen_buffer 快照重绘到终端。
#pragma once
#include <windows.h>
#include "miniio/io_thread.hpp"
#include "os/Console/conmsgl1.h"
#include "api_handlers.hpp"
#include "screen_buffer.hpp"
#include "input_buffer.hpp"
#include "io_state.hpp"
#include "viewport_render.hpp"
#include "utility/log.hpp"

namespace corehost::conpty
{

struct api_router
{
    // state 保存 Console API 可见的全局状态：模式、代码页、光标、标题、当前属性等。
    console_state &state;
    // sb_main 是默认 screen buffer；普通 Console API 和主缓冲区 VT 输出读写它。
    screen_buffer &sb_main;
    // sb_alt 是 DECSET 1049 备用缓冲区；切换后同一套 API handler 操作它。
    screen_buffer &sb_alt;
    // inp 是 GetConsoleInput/WriteConsoleInput 可见的 INPUT_RECORD 队列。
    input_buffer &inp;
    // io 用于根据 descriptor.Object 判断请求来自 input 还是 output 客户端句柄。
    io_state &io;
    // bridge 用于把 API 造成的状态变化同步为 VT 输出，并处理 ReadConsole 挂起。
    pipe_bridge &bridge;
    // false 使用主缓冲区；true 使用备用缓冲区。
    bool alt_active = false;

    // 返回当前 Console API 应读写的 screen_buffer。返回引用由 alt_active 决定；
    // 调用方不能保存到切换之后继续使用。
    screen_buffer &active_screen_buffer() noexcept
    {
        // alt_active 只影响 Console API 读写哪个 screen_buffer；console_state
        // 的模式、标题、光标等元数据不随缓冲区切换而复制。
        return alt_active ? sb_alt : sb_main;
    }

    // 切换主/备用屏幕缓冲区，并同步宿主终端 alternate-buffer 状态。alt=true
    // 使用备用缓冲区；alt=false 回到主缓冲区。
    void switch_active_screen_buffer(bool alt)
    {
        // alt 来自 VT alternate-buffer 消息。true 切到备用缓冲区，false 回主缓冲区；
        // 该状态决定后续 Read/WriteConsoleOutput 观察哪份 screen_buffer。
        // alt 等于当前状态时不发送 VT，避免重复切换清空终端屏幕。
        if (alt == alt_active)
            return;

        LOG2("switch active screen buffer alt=%d previous=%d", alt, alt_active);
        alt_active = alt;
        bridge.set_active_screen_buffer(active_screen_buffer());

        // DECSET/DECRST 1049 让终端切换备用缓冲区。随后重绘本地 active
        // screen_buffer，保证终端内容与 libcorehost 内存状态一致。
        if (alt)
            bridge.vt_append_str("\x1b[?1049h"sv);
        else
            bridge.vt_append_str("\x1b[?1049l"sv);
        bridge.vt_flush();
        vt_write_screen_snapshot();
    }

    // 将当前 active screen buffer 的可见 viewport 写回宿主终端。函数只重绘
    // 终端输出，不改变 active buffer 内容或 alt_active。
    void vt_write_screen_snapshot()
    {
        // 快照重绘只输出 active buffer 的 viewport，不改变 screen_buffer。
        render_visible_viewport(state, active_screen_buffer(), bridge);
    }

    // 分派一条 CONSOLE_IO_USER_DEFINED 消息。返回 false 表示具体 handler
    // 挂起了请求，后续由 pipe_bridge 显式 COMPLETE_IO。
    bool handle_user_defined(miniio::io_msg &msg)
    {
        // msg 是 message_router 确认 Function==CONSOLE_IO_USER_DEFINED 后交进来的
        // 原始请求；本函数只解析 CONSOLE_MSG_HEADER 和 L1/L2/L3 API 描述符。
        // msg.body 必须以 CONSOLE_MSG_HEADER 开头；message_router 只把
        // CONSOLE_IO_USER_DEFINED 传到这里。
        if (msg.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER))
        {
            LOG2("USER_DEFINED rejected: inputSize=%lu headerSize=%zu", msg.descriptor.InputSize,
                 sizeof(CONSOLE_MSG_HEADER));
            miniio::prepare_completion(msg, status_illegal_function);
            return true;
        }

        auto *hdr = reinterpret_cast<CONSOLE_MSG_HEADER *>(msg.body);

        // ApiNumber 高字节为 L1/L2/L3 层号，低 24 位为该层 API 编号。
        auto layer = hdr->ApiNumber >> 24;
        auto api = hdr->ApiNumber & 0xFFFFFF;
        // required_size 是当前实现理解的最小 descriptor 大小；客户端可以带
        // 更大的输入载荷，但 descriptor 本身不能短于该 API 的固定头。
        auto required_size = api_descriptor_required_size(layer, api);
        LOG2("USER_DEFINED enter apiNumber=0x%08lx layer=%lu api=%lu descriptorSize=%lu required=%zu inputSize=%lu "
             "outputSize=%lu",
             hdr->ApiNumber, layer, api, hdr->ApiDescriptorSize, required_size, msg.descriptor.InputSize,
             msg.descriptor.OutputSize);
        if (required_size == invalid_api_descriptor_size || hdr->ApiDescriptorSize > sizeof(msg.body) ||
            hdr->ApiDescriptorSize > msg.descriptor.InputSize - sizeof(CONSOLE_MSG_HEADER) ||
            hdr->ApiDescriptorSize < required_size)
        {
            LOG2("USER_DEFINED rejected: layer=%lu api=%lu descriptorSize=%lu required=%zu inputSize=%lu", layer, api,
                 hdr->ApiDescriptorSize, required_size, msg.descriptor.InputSize);
            miniio::prepare_completion(msg, status_illegal_function);
            return true;
        }

        // completed=false 表示 handler 已挂起请求；io_loop 不会提交 msg.complete，
        // 等 bridge 在收到 VT 输入后显式 COMPLETE_IO。
        bool completed = true;
        switch (layer)
        {
        case 1:
            completed = dispatch_L1(msg, api);
            break;
        case 2:
            completed = dispatch_L2(msg, api);
            break;
        case 3:
            completed = dispatch_L3(msg, api);
            break;
        default:
            LOG2("USER_DEFINED rejected: unsupported layer=%lu api=%lu", layer, api);
            miniio::prepare_completion(msg, status_illegal_function);
            return true;
        }
        LOG2("USER_DEFINED consumed layer=%lu api=%lu completedInline=%d status=0x%08lx information=%llu", layer, api,
             completed, msg.complete.IoStatus.Status, msg.complete.IoStatus.Information);
        return completed;
    }

    // ── L1 / L2 / L3 分发表 ──

    static constexpr size_t invalid_api_descriptor_size = static_cast<size_t>(-1);

    // 返回当前实现接受的固定 API descriptor 最小大小。ConDrv 消息允许 body
    // 后面追加变长输入/输出数据，但固定 descriptor 不能短于对应结构，否则
    // handler 会按错误布局解释 msg.body。
    constexpr size_t api_descriptor_required_size(DWORD layer, DWORD api) const noexcept
    {
        switch (layer)
        {
        case 1:
            switch (api)
            {
            case 0:
                return sizeof(CONSOLE_GETCP_MSG);
            case 1:
            case 2:
                return sizeof(CONSOLE_MODE_MSG);
            case 3:
                return sizeof(CONSOLE_GETNUMBEROFINPUTEVENTS_MSG);
            case 4:
                return sizeof(CONSOLE_GETCONSOLEINPUT_MSG);
            case 5:
                return sizeof(CONSOLE_READCONSOLE_MSG);
            case 6:
                return sizeof(CONSOLE_WRITECONSOLE_MSG);
            case 7:
                return 0;
            case 8:
                return sizeof(CONSOLE_LANGID_MSG);
            case 9:
                return sizeof(CONSOLE_MAPBITMAP_MSG);
            default:
                return invalid_api_descriptor_size;
            }
        case 2:
            switch (api)
            {
            case 0:
                return sizeof(CONSOLE_FILLCONSOLEOUTPUT_MSG);
            case 1:
                return sizeof(CONSOLE_CTRLEVENT_MSG);
            case 2:
            case 3:
                return 0;
            case 4:
                return sizeof(CONSOLE_SETCP_MSG);
            case 5:
                return sizeof(CONSOLE_GETCURSORINFO_MSG);
            case 6:
                return sizeof(CONSOLE_SETCURSORINFO_MSG);
            case 7:
            case 8:
                return sizeof(CONSOLE_SCREENBUFFERINFO_MSG);
            case 9:
                return sizeof(CONSOLE_SETSCREENBUFFERSIZE_MSG);
            case 10:
                return sizeof(CONSOLE_SETCURSORPOSITION_MSG);
            case 11:
                return sizeof(CONSOLE_GETLARGESTWINDOWSIZE_MSG);
            case 12:
                return sizeof(CONSOLE_SCROLLSCREENBUFFER_MSG);
            case 13:
                return sizeof(CONSOLE_SETTEXTATTRIBUTE_MSG);
            case 14:
                return sizeof(CONSOLE_SETWINDOWINFO_MSG);
            case 15:
                return sizeof(CONSOLE_READCONSOLEOUTPUTSTRING_MSG);
            case 16:
                return sizeof(CONSOLE_WRITECONSOLEINPUT_MSG);
            case 17:
                return sizeof(CONSOLE_WRITECONSOLEOUTPUT_MSG);
            case 18:
                return sizeof(CONSOLE_WRITECONSOLEOUTPUTSTRING_MSG);
            case 19:
                return sizeof(CONSOLE_READCONSOLEOUTPUT_MSG);
            case 20:
                return sizeof(CONSOLE_GETTITLE_MSG);
            case 21:
                return sizeof(CONSOLE_SETTITLE_MSG);
            default:
                return invalid_api_descriptor_size;
            }
        case 3:
            switch (api)
            {
            case 0:
                return sizeof(CONSOLE_GETNUMBEROFFONTS_MSG);
            case 1:
                return sizeof(CONSOLE_GETMOUSEINFO_MSG);
            case 2:
                return sizeof(CONSOLE_GETFONTINFO_MSG);
            case 3:
                return sizeof(CONSOLE_GETFONTSIZE_MSG);
            case 4:
                return sizeof(CONSOLE_CURRENTFONT_MSG);
            case 5:
                return sizeof(CONSOLE_SETFONT_MSG);
            case 6:
                return sizeof(CONSOLE_SETICON_MSG);
            case 7:
                return sizeof(CONSOLE_INVALIDATERECT_MSG);
            case 8:
                return sizeof(CONSOLE_VDM_MSG);
            case 9:
                return sizeof(CONSOLE_SETCURSOR_MSG);
            case 10:
                return sizeof(CONSOLE_SHOWCURSOR_MSG);
            case 11:
                return sizeof(CONSOLE_MENUCONTROL_MSG);
            case 12:
                return sizeof(CONSOLE_SETPALETTE_MSG);
            case 13:
                return sizeof(CONSOLE_SETDISPLAYMODE_MSG);
            case 14:
                return sizeof(CONSOLE_REGISTERVDM_MSG);
            case 15:
                return sizeof(CONSOLE_GETHARDWARESTATE_MSG);
            case 16:
                return sizeof(CONSOLE_SETHARDWARESTATE_MSG);
            case 17:
                return sizeof(CONSOLE_GETDISPLAYMODE_MSG);
            case 18:
                return sizeof(CONSOLE_ADDALIAS_MSG);
            case 19:
                return sizeof(CONSOLE_GETALIAS_MSG);
            case 20:
                return sizeof(CONSOLE_GETALIASESLENGTH_MSG);
            case 21:
                return sizeof(CONSOLE_GETALIASEXESLENGTH_MSG);
            case 22:
                return sizeof(CONSOLE_GETALIASES_MSG);
            case 23:
                return sizeof(CONSOLE_GETALIASEXES_MSG);
            case 24:
                return sizeof(CONSOLE_EXPUNGECOMMANDHISTORY_MSG);
            case 25:
                return sizeof(CONSOLE_SETNUMBEROFCOMMANDS_MSG);
            case 26:
                return sizeof(CONSOLE_GETCOMMANDHISTORYLENGTH_MSG);
            case 27:
                return sizeof(CONSOLE_GETCOMMANDHISTORY_MSG);
            case 28:
                return sizeof(CONSOLE_SETKEYSHORTCUTS_MSG);
            case 29:
                return sizeof(CONSOLE_SETMENUCLOSE_MSG);
            case 30:
                return sizeof(CONSOLE_GETKEYBOARDLAYOUTNAME_MSG);
            case 31:
                return sizeof(CONSOLE_GETCONSOLEWINDOW_MSG);
            case 32:
                return sizeof(CONSOLE_CHAR_TYPE_MSG);
            case 33:
                return sizeof(CONSOLE_LOCAL_EUDC_MSG);
            case 34:
            case 35:
                return sizeof(CONSOLE_CURSOR_MODE_MSG);
            case 36:
                return sizeof(CONSOLE_REGISTEROS2_MSG);
            case 37:
                return sizeof(CONSOLE_SETOS2OEMFORMAT_MSG);
            case 38:
            case 39:
                return sizeof(CONSOLE_NLS_MODE_MSG);
            case 40:
                return sizeof(CONSOLE_GETSELECTIONINFO_MSG);
            case 41:
                return sizeof(CONSOLE_GETCONSOLEPROCESSLIST_MSG);
            case 42:
            case 43:
                return sizeof(CONSOLE_HISTORY_MSG);
            case 44:
                return sizeof(CONSOLE_CURRENTFONT_MSG);
            default:
                return invalid_api_descriptor_size;
            }
        default:
            return invalid_api_descriptor_size;
        }
    }

    // 分派 L1 API，并根据 descriptor.Object 判断 Get/SetConsoleMode 操作输入
    // 还是输出句柄。返回 false 表示 ReadConsole/GetConsoleInput 类请求已挂起。
    bool dispatch_L1(miniio::io_msg &msg, DWORD api)
    {
        // L1 包含代码页、模式、输入读取和 WriteConsole 等基础 API。
        auto &sb = active_screen_buffer();
        auto object_kind = io.kind_from_object(msg.descriptor.Object);
        const bool input_handle =
            object_kind != io_state::object_kind::output && object_kind != io_state::object_kind::alternate_output;
        // api==4 是 GetConsoleInput，PSReadLine 会高频轮询；不记录以免刷屏。
        if (api != 4)
            LOG2("[dispatch] L1 api=%lu", api);
        switch (api)
        {
        case 0:
            return api_get_cp(msg, state, sb, inp, bridge);
        case 1:
            return api_get_mode(msg, state, sb, inp, bridge, input_handle);
        case 2:
            return api_set_mode(msg, state, sb, inp, bridge, input_handle);
        case 3:
            return api_get_num_input(msg, state, sb, inp, bridge);
        case 4:
            return api_get_console_input(msg, state, sb, inp, bridge);
        case 5:
            return api_read_console(msg, state, sb, inp, bridge);
        case 6:
            return api_write_console(msg, state, sb, inp, bridge);
        case 7:
            return api_deprecated_l1(msg, state, sb, inp, bridge);
        case 8:
            return api_get_langid(msg, state, sb, inp, bridge);
        case 9:
            return api_deprecated_l1(msg, state, sb, inp, bridge);
        default:
            miniio::prepare_completion(msg, status_illegal_function);
            return true;
        }
    }

    // 分派 L2 API。所有 handler 都操作当前 active_screen_buffer，因此备用
    // 缓冲区激活时 Read/WriteConsoleOutput 会自然落到 sb_alt。
    bool dispatch_L2(miniio::io_msg &msg, DWORD api)
    {
        // L2 包含屏幕缓冲区、窗口、光标和标题等 API。
        auto &sb = active_screen_buffer();
        // api==7/13 是查询缓冲区信息/设置属性的高频路径。
        if (api != 7 && api != 13)
            LOG2("[dispatch] L2 api=%lu", api);
        switch (api)
        {
        case 0:
            return api_fill_output(msg, state, sb, inp, bridge);
        case 1:
            return api_ctrl_event(msg, state, sb, inp, bridge);
        case 2:
            switch (io.kind_from_object(msg.descriptor.Object))
            {
            case io_state::object_kind::output:
                switch_active_screen_buffer(false);
                return api_set_active_sb(msg, state, active_screen_buffer(), inp, bridge);
            case io_state::object_kind::alternate_output:
                switch_active_screen_buffer(true);
                return api_set_active_sb(msg, state, active_screen_buffer(), inp, bridge);
            default:
                miniio::prepare_completion(msg, status_invalid_parameter);
                return true;
            }
        case 3:
            return api_flush_input_buf(msg, state, sb, inp, bridge);
        case 4:
            return api_set_cp(msg, state, sb, inp, bridge);
        case 5:
            return api_get_cursor(msg, state, sb, inp, bridge);
        case 6:
            return api_set_cursor(msg, state, sb, inp, bridge);
        case 7:
            return api_get_sb_info(msg, state, sb, inp, bridge);
        case 8:
            return api_set_sb_info(msg, state, sb, inp, bridge);
        case 9:
            return api_set_sb_size(msg, state, sb, inp, bridge);
        case 10:
            return api_set_cursor_pos(msg, state, sb, inp, bridge);
        case 11:
            return api_largest_window(msg, state, sb, inp, bridge);
        case 12:
            return api_scroll_sb(msg, state, sb, inp, bridge);
        case 13:
            return api_set_text_attr(msg, state, sb, inp, bridge);
        case 14:
            return api_set_window_info(msg, state, sb, inp, bridge);
        case 15:
            return api_read_output_string(msg, state, sb, inp, bridge);
        case 16:
            return api_write_console_input(msg, state, sb, inp, bridge);
        case 17:
            return api_write_console_output(msg, state, sb, inp, bridge);
        case 18:
            return api_write_output_string(msg, state, sb, inp, bridge);
        case 19:
            return api_read_console_output(msg, state, sb, inp, bridge);
        case 20:
            return api_get_title(msg, state, sb, inp, bridge);
        case 21:
            return api_set_title(msg, state, sb, inp, bridge);
        default:
            miniio::prepare_completion(msg, status_illegal_function);
            return true;
        }
    }

    // 分派 L3 扩展 API。活跃 API 读写 console_state、bridge 历史/别名/进程
    // 快照；废弃 API 返回兼容 completion，不维护额外内部状态。
    bool dispatch_L3(miniio::io_msg &msg, DWORD api)
    {
        // L3 覆盖鼠标、字体、别名、历史、进程列表等扩展 API。
        auto &sb = active_screen_buffer();
        // api==31/4 分别是 GetConsoleWindow/GetCurrentFont 的常见轮询路径。
        if (api != 31 && api != 4)
            LOG2("[dispatch] L3 api=%lu", api);
        switch (api)
        {
        // ── 第一类: 活跃 L3 API (20 个) ──
        // 这些 API 在当前实现中读写 console_state、pipe_bridge 或 input_buffer。
        case 1:
            return api_l3_get_mouse_info(msg, state, sb, inp, bridge);
        case 3:
            return api_l3_get_font_size(msg, state, sb, inp, bridge);
        case 4:
            return api_l3_get_current_font(msg, state, sb, inp, bridge);
        case 13:
            return api_l3_set_display_mode(msg, state, sb, inp, bridge);
        case 17:
            return api_l3_get_display_mode(msg, state, sb, inp, bridge);
        case 18:
            return api_l3_add_alias(msg, state, sb, inp, bridge);
        case 19:
            return api_l3_get_alias(msg, state, sb, inp, bridge);
        case 20:
            return api_l3_get_aliases_length(msg, state, sb, inp, bridge);
        case 21:
            return api_l3_get_alias_exes_length(msg, state, sb, inp, bridge);
        case 22:
            return api_l3_get_aliases(msg, state, sb, inp, bridge);
        case 23:
            return api_l3_get_alias_exes(msg, state, sb, inp, bridge);
        case 24:
            return api_l3_expunge_history(msg, state, sb, inp, bridge);
        case 25:
            return api_l3_set_num_commands(msg, state, sb, inp, bridge);
        case 26:
            return api_l3_get_history_length(msg, state, sb, inp, bridge);
        case 27:
            return api_l3_get_history(msg, state, sb, inp, bridge);
        case 31:
            return api_l3_get_console_window(msg, state, sb, inp, bridge);
        case 40:
            return api_l3_get_selection_info(msg, state, sb, inp, bridge);
        case 41:
            return api_l3_get_process_list(msg, state, sb, inp, bridge);
        case 42:
            return api_l3_get_history_info(msg, state, sb, inp, bridge);
        case 43:
            return api_l3_set_history_info(msg, state, sb, inp, bridge);
        case 44:
            return api_l3_set_current_font(msg, state, sb, inp, bridge);

        // ── 第二类: 废弃 L3 API (24 个) ──
        // 这些 API 保留入口但不维护真实状态，统一走 deprecated completion。
        case 0:
        case 2:
        case 5:
        case 6:
        case 7:
        case 8:
        case 9:
        case 10:
        case 11:
        case 12:
        case 14:
        case 15:
        case 16:
        case 28:
        case 29:
        case 30:
        case 32:
        case 33:
        case 34:
        case 35:
        case 36:
        case 37:
        case 38:
        case 39:
            return api_l3_deprecated(msg, state, sb, inp, bridge);

        default:
            miniio::prepare_completion(msg, status_illegal_function);
            return true;
        }
    }
};

} // namespace corehost::conpty
