#include "deftermv2.hpp"

#include <windows.h>
#include <cassert>
#include <utility>
#include "defterm/connect.hpp"
#include "defterm/io_loop.hpp"
#include "miniio/io_thread.hpp"
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

deftermv2_result deftermv2_entry(std::uintptr_t condrv_handle)
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
        return {};

    deftermv2_result result;
    result.server = std::move(server);
    result.event = win32::handle{ev.release()};
    result.condrv_input = std::move(handler.condrv_input);
    result.condrv_output = std::move(handler.condrv_output);
    result.initial_connect = handler.initial_connect;
    result.has_initial_connect = handler.has_initial_connect;
    initialize_vt_handles(result.vt_in, result.vt_out, result.vt_in_keepalive);
    result.width = handler.width;
    result.height = handler.height;
    LOG("deftermv2_entry: returning conpty result vtIn=%p vtOut=%p vtInKeepalive=%p input=%p output=%p w=%d h=%d",
        result.vt_in.get(), result.vt_out.get(), result.vt_in_keepalive.get(), result.condrv_input.get(),
        result.condrv_output.get(), result.width, result.height);
    return result;
}

} // namespace deftermv2
