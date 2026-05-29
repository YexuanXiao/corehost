#include "deftermv2.hpp"

#include <windows.h>
#include <cassert>
#include <utility>
#include "defterm/connect.hpp"
#include "defterm/io_loop.hpp"
#include "miniio/io_thread.hpp"
#include "io_loop.hpp"
#include "console_state.hpp"
#include "screen_buffer.hpp"
#include "input_buffer.hpp"
#include "io_state.hpp"
#include "pipe_bridge.hpp"
#include "api_router.hpp"
#include "message_router.hpp"
#include "connect_completion.hpp"
#include "default_console_size.hpp"
#include "text_measurement_mode.hpp"
#include "utility/log.hpp"
#include "win32/event.hpp"
#include "win32/handle.hpp"

namespace deftermv2
{

win32::handle valid_std_handle(DWORD std_handle_id) noexcept
{
    auto handle = ::GetStdHandle(std_handle_id);
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE)
        return {};
    return win32::handle{handle};
}

win32::handle open_null_output()
{
    win32::handle output{::CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
    win32::throw_last_error(!output.valid());
    return output;
}

void initialize_vt_handles(win32::handle &input, win32::handle &output, win32::handle &input_keepalive)
{
    input = valid_std_handle(STD_INPUT_HANDLE);
    output = valid_std_handle(STD_OUTPUT_HANDLE);

    if (!input.valid())
    {
        if (!::CreatePipe(reinterpret_cast<PHANDLE>(input.put()), reinterpret_cast<PHANDLE>(input_keepalive.put()),
                          nullptr, 0))
            win32::throw_last_error();
        LOG("deftermv2::initialize_vt_handles: using empty input pipe read=%p keepalive=%p", input.get(),
            input_keepalive.get());
    }

    if (!output.valid())
    {
        output = open_null_output();
        LOG("deftermv2::initialize_vt_handles: using NUL output=%p", output.get());
    }
}

void copy_process_list(conpty::io_state &io, conpty::pipe_bridge &bridge)
{
    bridge.proc_count = io.process_count;
    for (size_t i = 0; i < io.process_count; ++i)
        bridge.proc_list[i] = io.process_list[i];
}

void run_conpty(win32::handle server, win32::handle event, win32::handle vt_in, win32::handle vt_out,
                win32::handle vt_in_keepalive, miniio::io_msg &initial_connect, short width, short height)
{
    const bool poll_vt_input = vt_in_keepalive.valid();
    LOG("deftermv2::run_conpty: s=%p vi=%p vo=%p ev=%p keepalive=%p w=%d h=%d pollVt=%d", server.get(), vt_in.get(),
        vt_out.get(), event.get(), vt_in_keepalive.get(), width, height, poll_vt_input);

    conpty::console_state state;
    state.screen_buffer_size.X = width > 0 ? width : conpty::default_console_size.X;
    state.screen_buffer_size.Y = height > 0 ? height : conpty::default_console_size.Y;
    state.current_window_size = state.screen_buffer_size;
    state.max_window_size = state.screen_buffer_size;
    state.cursor.position = {0, 0};
    state.text_measurement = conpty::text_measurement_mode::graphemes;
    state.ambiguous_is_wide = true;
    state.init_tab_stops();

    conpty::screen_buffer sbuf(state.screen_buffer_size);
    sbuf.clear(state.default_attributes);

    conpty::screen_buffer alt_sbuf(state.screen_buffer_size);
    alt_sbuf.clear(state.default_attributes);

    conpty::input_buffer ibuf;
    ibuf.init_event();

    conpty::io_state io;
    io.set_server(server.view());

    conpty::pipe_bridge bridge{ibuf, state, sbuf};
    bridge.vt_in = vt_in.view();
    bridge.vt_out = vt_out.view();
    bridge.server = server.view();

    conpty::api_router api{state, sbuf, alt_sbuf, ibuf, io, bridge};
    conpty::message_router router{io, bridge, api};

    win32::event input_poll_event;
    if (poll_vt_input)
    {
        input_poll_event = win32::event{win32::create_tag, true, false};
        bridge.set_signal_shutdown_event(input_poll_event.view());
    }

    bridge.vt_append_str("\x1b[?9001h");
    bridge.vt_flush();
    LOG("deftermv2::run_conpty: sent Win32Input init sequence");

    LOG("deftermv2::run_conpty: completing initial CONNECT id=%08lx:%08lx",
        initial_connect.descriptor.Identifier.HighPart, initial_connect.descriptor.Identifier.LowPart);
    conpty::connect_completion completion = conpty::connect_completion::explicit_complete;
    io.handle_connect(initial_connect, completion);
    copy_process_list(io, bridge);

    LOG("deftermv2::run_conpty: entering io loop");
    conpty::run_io_loop_no_setup(server.view(), event.view(), router);
    LOG("deftermv2::run_conpty: loop returned");
}

struct connect_handler
{
    bool initialized = false;
    bool start_conpty = false;
    short width = 0;
    short height = 0;
    win32::handle condrv_input;
    win32::handle condrv_output;
    miniio::io_msg initial_connect;
    bool has_initial_connect = false;
    win32::handle_view server;
    win32::handle_view ev;
    defterm::fallback_state fallback;

    bool on_connect(miniio::io_msg &msg, defterm::connect_completion &completion)
    {
        completion = defterm::connect_completion::explicit_complete;

        const auto client_pid = static_cast<DWORD>(msg.descriptor.Process);
        auto &connect_info = *reinterpret_cast<const CONSOLE_SERVER_MSG *>(msg.body);
        LOG("deftermv2::on_connect: pid=%lu pgid=%lu initialized=%d consoleApp=%u visible=%u show=%u flags=0x%08lx",
            client_pid, connect_info.ProcessGroupId, initialized, static_cast<unsigned>(connect_info.ConsoleApp),
            static_cast<unsigned>(connect_info.WindowVisible), connect_info.ShowWindow, connect_info.StartupFlags);

        const bool need_gui = defterm::should_open_terminal_window(connect_info, initialized);
        initialized = true;

        if (!need_gui)
        {
            LOG("deftermv2::on_connect: no GUI requested, deferring initial CONNECT to conpty");
            initial_connect = msg;
            has_initial_connect = true;
            width = connect_info.ScreenBufferSize.X > 0 ? connect_info.ScreenBufferSize.X : 0;
            height = connect_info.ScreenBufferSize.Y > 0 ? connect_info.ScreenBufferSize.Y : 0;
            start_conpty = true;
            return false;
        }

        if (env::is_elevated())
        {
            LOG("deftermv2::on_connect: elevated fallback");
            auto image_path = defterm::query_process_image_path(client_pid);
            LOG(L"deftermv2::on_connect: elevated process imagePath=%ls", image_path.c_str());
            env::show_elevated_notification(image_path);
            miniio::accept_connection(server, msg, condrv_input, condrv_output);
            fallback.break_when_input_waits = true;
            fallback.target_process_group_id = connect_info.ProcessGroupId ? connect_info.ProcessGroupId : client_pid;
            return true;
        }

        LOG("deftermv2::on_connect: trying COM handoff");
        if (defterm::try_handoff_all(server, ev, defterm::make_portable_attach_msg(msg), client_pid))
        {
            LOG("deftermv2::on_connect: handoff success");
            return false;
        }

        LOG("deftermv2::on_connect: no terminal fallback");
        env::show_not_found_notification();
        miniio::accept_connection(server, msg, condrv_input, condrv_output);
        ::GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, client_pid);
        return true;
    }

    bool on_message(miniio::io_msg &msg)
    {
        LOG("deftermv2::on_message: func=%lu fallbackWait=%d breakSent=%d", msg.descriptor.Function,
            fallback.break_when_input_waits, fallback.break_sent);
        defterm::send_deferred_ctrl_break_if_needed(msg, fallback);
        defterm::dispatch_non_connect(server, msg, condrv_input, condrv_output);
        return true;
    }

    void on_idle() noexcept
    {
    }

    bool has_pending() const noexcept
    {
        return false;
    }

    bool should_exit() const noexcept
    {
        return false;
    }
};

void deftermv2_entry(std::uintptr_t condrv_handle)
{
    LOG("deftermv2_entry: start, handle=0x%Ix", condrv_handle);
    assert(condrv_handle != 0);

    auto server = win32::handle{reinterpret_cast<HANDLE>(condrv_handle)};
    auto ev = win32::event{win32::create_tag, true, false};

    connect_handler handler{};
    handler.server = server.view();
    handler.ev = ev.view();

    defterm::run_io_loop(server.view(), ev.view(), handler);
    LOG("deftermv2_entry: run_io_loop returned startConpty=%d", handler.start_conpty);

    if (!handler.start_conpty)
        return;

    assert(handler.has_initial_connect);
    win32::handle vt_in;
    win32::handle vt_out;
    win32::handle vt_in_keepalive;
    initialize_vt_handles(vt_in, vt_out, vt_in_keepalive);
    run_conpty(std::move(server), win32::handle{ev.release()}, std::move(vt_in), std::move(vt_out),
               std::move(vt_in_keepalive), handler.initial_connect, handler.width, handler.height);
}

} // namespace deftermv2
