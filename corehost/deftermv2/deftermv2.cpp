#include "deftermv2.hpp"

#include <windows.h>
#include <cassert>
#include <utility>
#include "connect_policy.hpp"
#include "mini_fallback.hpp"
#include "terminal_handoff.hpp"
#include "vt_handles.hpp"
#include "conpty.hpp"
#include "miniio/io_thread.hpp"
#include "os/Console/conmsgl1.h"
#include "utility/env.hpp"
#include "utility/log.hpp"
#include "win32/event.hpp"
#include "win32/handle.hpp"

namespace deftermv2
{

enum class initial_connect_completion
{
    explicit_complete,
    inline_complete,
};

struct connect_handler
{
    bool initialized = false;
    bool start_conpty = false;
    short width = 0;
    short height = 0;
    DWORD attached_process_id = 0;
    win32::handle condrv_input;
    win32::handle condrv_output;
    win32::handle_view server;
    win32::handle_view input_event;
    mini_fallback_state fallback;

    bool on_connect(miniio::io_msg &msg, initial_connect_completion &completion)
    {
        completion = initial_connect_completion::explicit_complete;

        const auto client_pid = static_cast<DWORD>(msg.descriptor.Process);
        auto &connect_info = *reinterpret_cast<const CONSOLE_SERVER_MSG *>(msg.body);
        LOG("deftermv2::connect_handler::on_connect: pid=%lu pgid=%lu initialized=%d consoleApp=%u visible=%u "
            "show=%u flags=0x%08lx",
            client_pid, connect_info.ProcessGroupId, initialized, static_cast<unsigned>(connect_info.ConsoleApp),
            static_cast<unsigned>(connect_info.WindowVisible), connect_info.ShowWindow, connect_info.StartupFlags);

        const bool need_gui = should_start_terminal_window(connect_info, initialized);
        initialized = true;

        if (!need_gui)
        {
            LOG("deftermv2::connect_handler::on_connect: no GUI requested, switching to conpty");
            width = connect_info.ScreenBufferSize.X > 0 ? connect_info.ScreenBufferSize.X : 0;
            height = connect_info.ScreenBufferSize.Y > 0 ? connect_info.ScreenBufferSize.Y : 0;
            attached_process_id = client_pid;
            miniio::accept_connection(server, msg, condrv_input, condrv_output);
            start_conpty = true;
            return false;
        }

        if (env::is_elevated())
        {
            LOG("deftermv2::connect_handler::on_connect: elevated fallback");
            auto image_path = query_process_image_path(client_pid);
            LOG(L"deftermv2::connect_handler::on_connect: elevated process imagePath=%ls", image_path.c_str());
            env::show_elevated_notification(image_path);
            miniio::accept_connection(server, msg, condrv_input, condrv_output);
            fallback.break_when_input_waits = true;
            fallback.target_process_group_id = connect_info.ProcessGroupId ? connect_info.ProcessGroupId : client_pid;
            return true;
        }

        LOG("deftermv2::connect_handler::on_connect: trying COM handoff");
        if (try_terminal_handoff(server, input_event, make_portable_attach_msg(msg), client_pid))
        {
            LOG("deftermv2::connect_handler::on_connect: handoff success");
            return false;
        }

        LOG("deftermv2::connect_handler::on_connect: no terminal fallback");
        env::show_not_found_notification();
        miniio::accept_connection(server, msg, condrv_input, condrv_output);
        ::GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, client_pid);
        return true;
    }

    bool on_message(miniio::io_msg &msg)
    {
        LOG("deftermv2::connect_handler::on_message: func=%lu fallbackWait=%d breakSent=%d", msg.descriptor.Function,
            fallback.break_when_input_waits, fallback.break_sent);
        send_deferred_ctrl_break_if_needed(msg, fallback);
        dispatch_mini_fallback_message(server, msg, condrv_input, condrv_output);
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

void run_initial_connect_loop(win32::handle_view server, win32::handle_view input_event, connect_handler &handler)
{
    LOG("deftermv2::run_initial_connect_loop: enter server=%p event=%p", server.get(), input_event.get());
    miniio::set_server_info(server, input_event);

    miniio::io_msg message_a{};
    miniio::io_msg message_b{};
    miniio::io_msg *current = &message_a;
    miniio::io_msg *completed_previous = nullptr;

    for (;;)
    {
        if (!handler.has_pending() && completed_previous == nullptr)
        {
            handler.on_idle();
            if (handler.should_exit())
                break;
            if (!handler.has_pending())
                ::WaitForSingleObject(input_event.get(), 1);
        }

        auto *completion = completed_previous ? &completed_previous->complete : nullptr;
        if (completion)
        {
            LOG("deftermv2::run_initial_connect_loop: read submits completion id=%08lx:%08lx status=0x%08lx info=%llu",
                completion->Identifier.HighPart, completion->Identifier.LowPart,
                static_cast<unsigned long>(completion->IoStatus.Status),
                static_cast<unsigned long long>(completion->IoStatus.Information));
        }
        else
        {
            LOG("deftermv2::run_initial_connect_loop: read without completion");
        }

        const auto read_result = miniio::read_io_try(server, completion, *current);
        if (read_result == miniio::read_io_result::disconnected)
        {
            LOG("deftermv2::run_initial_connect_loop: disconnected");
            break;
        }
        if (read_result == miniio::read_io_result::no_message)
        {
            completed_previous = nullptr;
            handler.on_idle();
            if (handler.should_exit())
                break;
            continue;
        }
        completed_previous = nullptr;

        LOG("deftermv2::run_initial_connect_loop: got func=%lu id=%08lx:%08lx pid=%llu object=%llu in=%lu out=%lu",
            current->descriptor.Function, current->descriptor.Identifier.HighPart,
            current->descriptor.Identifier.LowPart, static_cast<unsigned long long>(current->descriptor.Process),
            static_cast<unsigned long long>(current->descriptor.Object), current->descriptor.InputSize,
            current->descriptor.OutputSize);

        if (current->descriptor.Function == 0)
        {
            handler.on_idle();
            if (handler.should_exit())
                break;
            continue;
        }

        if (current->descriptor.Function == CONSOLE_IO_CONNECT)
        {
            initial_connect_completion connect_result = initial_connect_completion::explicit_complete;
            if (!handler.on_connect(*current, connect_result))
            {
                LOG("deftermv2::run_initial_connect_loop: CONNECT handler requested exit");
                return;
            }
            completed_previous = connect_result == initial_connect_completion::inline_complete ? current : nullptr;
            current = current == &message_a ? &message_b : &message_a;
            continue;
        }

        if (handler.on_message(*current))
        {
            completed_previous = current;
            handler.on_idle();
            if (handler.should_exit())
                break;
        }
        else
        {
            while (handler.has_pending())
            {
                handler.on_idle();
                if (handler.has_pending())
                    ::WaitForSingleObject(input_event.get(), 16);
            }
            if (handler.should_exit())
                break;
        }
        current = current == &message_a ? &message_b : &message_a;
    }

    LOG("deftermv2::run_initial_connect_loop: exit");
}

void deftermv2_entry(std::uintptr_t condrv_handle)
{
    LOG("deftermv2_entry: start, handle=0x%Ix", condrv_handle);
    assert(condrv_handle != 0);

    auto server = win32::handle{reinterpret_cast<HANDLE>(condrv_handle)};
    auto input_event = win32::event{win32::create_tag, true, false};

    connect_handler handler{};
    handler.server = server.view();
    handler.input_event = input_event.view();

    run_initial_connect_loop(server.view(), input_event.view(), handler);
    LOG("deftermv2_entry: initial loop returned startConpty=%d", handler.start_conpty);

    if (!handler.start_conpty)
        return;

    win32::handle vt_in;
    win32::handle vt_out;
    win32::handle vt_in_keepalive;
    initialize_vt_handles(vt_in, vt_out, vt_in_keepalive);

    conpty::conpty_session_config config;
    config.width = handler.width;
    config.height = handler.height;
    config.text_measurement = conpty::text_measurement_mode::graphemes;
    config.ambiguous_is_wide = true;
    config.poll_vt_input = vt_in_keepalive.valid();
    config.attached_process_id = handler.attached_process_id;

    conpty::run_conpty_session(std::move(server), win32::handle{input_event.release()}, std::move(handler.condrv_input),
                               std::move(handler.condrv_output), std::move(vt_in), std::move(vt_out), {}, config);
}

} // namespace deftermv2
