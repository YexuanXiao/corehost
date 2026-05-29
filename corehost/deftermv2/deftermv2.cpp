#include "deftermv2.hpp"

#include <windows.h>
#include <objbase.h>
#include <cassert>
#include <array>
#include <memory>
#include <string>
#include <utility>
#include "defterm/signal.hpp"
#include "IConsoleHandoff.h"
#include "api_router.hpp"
#include "com/clsid.hpp"
#include "com/com_ptr.hpp"
#include "connect_completion.hpp"
#include "console_state.hpp"
#include "default_console_size.hpp"
#include "input_buffer.hpp"
#include "io_loop.hpp"
#include "io_state.hpp"
#include "message_router.hpp"
#include "miniio/io_thread.hpp"
#include "ntapi/condrv.hpp"
#include "os/Console/conmsgl1.h"
#include "os/Console/condrv.h"
#include "pipe_bridge.hpp"
#include "screen_buffer.hpp"
#include "text_measurement_mode.hpp"
#include "utility/env.hpp"
#include "utility/log.hpp"
#include "win32/com_apartment.hpp"
#include "win32/error.hpp"
#include "win32/event.hpp"
#include "win32/handle.hpp"
#include "win32/hresult.hpp"
#include "win32/process.hpp"
#include "win32/thread.hpp"

namespace deftermv2
{
using namespace std::literals;

enum class initial_connect_completion
{
    explicit_complete,
    inline_complete,
};

struct mini_fallback_state
{
    bool break_when_input_waits = false;
    bool break_sent = false;
    DWORD target_process_group_id = 0;
};

[[nodiscard]] const wchar_t *io_function_name(ULONG function) noexcept
{
    switch (function)
    {
    case 0:
        return L"None";
    case CONSOLE_IO_CONNECT:
        return L"CONNECT";
    case CONSOLE_IO_DISCONNECT:
        return L"DISCONNECT";
    case CONSOLE_IO_CREATE_OBJECT:
        return L"CREATE_OBJECT";
    case CONSOLE_IO_CLOSE_OBJECT:
        return L"CLOSE_OBJECT";
    case CONSOLE_IO_RAW_WRITE:
        return L"RAW_WRITE";
    case CONSOLE_IO_RAW_READ:
        return L"RAW_READ";
    case CONSOLE_IO_USER_DEFINED:
        return L"USER_DEFINED";
    case CONSOLE_IO_RAW_FLUSH:
        return L"RAW_FLUSH";
    default:
        return L"UNKNOWN";
    }
}

[[nodiscard]] const wchar_t *show_window_name(WORD value) noexcept
{
    switch (value)
    {
    case SW_HIDE:
        return L"SW_HIDE";
    case SW_SHOWNORMAL:
        return L"SW_SHOWNORMAL";
    case SW_SHOWMINIMIZED:
        return L"SW_SHOWMINIMIZED";
    case SW_SHOWMAXIMIZED:
        return L"SW_SHOWMAXIMIZED";
    case SW_SHOWNOACTIVATE:
        return L"SW_SHOWNOACTIVATE";
    case SW_SHOW:
        return L"SW_SHOW";
    case SW_MINIMIZE:
        return L"SW_MINIMIZE";
    case SW_SHOWMINNOACTIVE:
        return L"SW_SHOWMINNOACTIVE";
    case SW_SHOWNA:
        return L"SW_SHOWNA";
    case SW_RESTORE:
        return L"SW_RESTORE";
    case SW_SHOWDEFAULT:
        return L"SW_SHOWDEFAULT";
    case SW_FORCEMINIMIZE:
        return L"SW_FORCEMINIMIZE";
    default:
        return L"UNKNOWN";
    }
}

[[nodiscard]] bool is_interactive_user_session() noexcept
{
    DWORD session_id = 0;
    if (!::ProcessIdToSessionId(::GetCurrentProcessId(), &session_id))
    {
        LOG("deftermv2::is_interactive_user_session: ProcessIdToSessionId failed err=%lu", ::GetLastError());
        return false;
    }
    if (session_id == 0)
    {
        LOG("deftermv2::is_interactive_user_session: session 0");
        return false;
    }

    auto window_station = ::GetProcessWindowStation();
    if (!window_station)
    {
        LOG("deftermv2::is_interactive_user_session: GetProcessWindowStation failed err=%lu", ::GetLastError());
        return false;
    }

    USEROBJECTFLAGS flags{};
    if (!::GetUserObjectInformationW(window_station, UOI_FLAGS, &flags, sizeof(flags), nullptr))
    {
        LOG("deftermv2::is_interactive_user_session: GetUserObjectInformationW failed err=%lu", ::GetLastError());
        return false;
    }
    if (!(flags.dwFlags & WSF_VISIBLE))
    {
        LOG("deftermv2::is_interactive_user_session: invisible window station flags=0x%08lx", flags.dwFlags);
        return false;
    }

    LOG("deftermv2::is_interactive_user_session: yes session=%lu flags=0x%08lx", session_id, flags.dwFlags);
    return true;
}

[[nodiscard]] bool connect_requests_terminal_window(const CONSOLE_SERVER_MSG &msg) noexcept
{
    LOG("deftermv2::connect_requests_terminal_window: consoleApp=%u visible=%u startupFlags=0x%08lx "
        "showWindow=%ls(%u) titleLength=%u pgid=%lu",
        static_cast<unsigned>(msg.ConsoleApp), static_cast<unsigned>(msg.WindowVisible), msg.StartupFlags,
        show_window_name(msg.ShowWindow), msg.ShowWindow, msg.TitleLength, msg.ProcessGroupId);

    if (!msg.WindowVisible)
    {
        LOG("deftermv2::connect_requests_terminal_window: reject WindowVisible=false");
        return false;
    }

    if (msg.StartupFlags & STARTF_USESHOWWINDOW)
    {
        switch (msg.ShowWindow)
        {
        case SW_HIDE:
        case SW_SHOWMINIMIZED:
        case SW_MINIMIZE:
        case SW_SHOWMINNOACTIVE:
        case SW_FORCEMINIMIZE:
            LOG("deftermv2::connect_requests_terminal_window: reject showWindow=%ls(%u)",
                show_window_name(msg.ShowWindow), msg.ShowWindow);
            return false;
        default:
            break;
        }
    }

    return true;
}

[[nodiscard]] bool should_start_terminal_window(const CONSOLE_SERVER_MSG &connect_info, bool already_initialized)
{
    if (already_initialized)
    {
        LOG("deftermv2::should_start_terminal_window: reject already initialized");
        return false;
    }
    if (!connect_requests_terminal_window(connect_info))
    {
        LOG("deftermv2::should_start_terminal_window: reject startup info");
        return false;
    }
    if (!is_interactive_user_session())
    {
        LOG("deftermv2::should_start_terminal_window: reject non-interactive session");
        return false;
    }
    return true;
}

[[nodiscard]] std::wstring query_process_image_path(DWORD pid)
{
    win32::handle process{::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)};
    win32::throw_last_error(!process.valid());
    return win32::query_full_process_image_name(process);
}

[[nodiscard]] bool is_waiting_for_user_input(const miniio::io_msg &msg) noexcept
{
    if (msg.descriptor.Function == CONSOLE_IO_RAW_READ)
    {
        LOG("deftermv2::is_waiting_for_user_input: RAW_READ");
        return true;
    }
    if (msg.descriptor.Function != CONSOLE_IO_USER_DEFINED ||
        msg.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLE_MSG))
        return false;

    auto *header = reinterpret_cast<const CONSOLE_MSG_HEADER *>(msg.body);
    LOG("deftermv2::is_waiting_for_user_input: USER_DEFINED api=0x%08lx", header->ApiNumber);
    return header->ApiNumber == ConsolepReadConsole;
}

void send_deferred_ctrl_break_if_needed(const miniio::io_msg &msg, mini_fallback_state &fallback)
{
    if (!fallback.break_when_input_waits || fallback.break_sent || !is_waiting_for_user_input(msg))
        return;

    LOG("deftermv2::fallback: sending deferred CTRL_BREAK to pgid=%lu", fallback.target_process_group_id);
    ::GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, fallback.target_process_group_id);
    fallback.break_sent = true;
}

[[nodiscard]] CONSOLE_PORTABLE_ATTACH_MSG make_portable_attach_msg(const miniio::io_msg &msg) noexcept
{
    CONSOLE_PORTABLE_ATTACH_MSG portable{};
    portable.IdLowPart = msg.descriptor.Identifier.LowPart;
    portable.IdHighPart = msg.descriptor.Identifier.HighPart;
    portable.Process = msg.descriptor.Process;
    portable.Object = msg.descriptor.Object;
    portable.Function = msg.descriptor.Function;
    portable.InputSize = msg.descriptor.InputSize;
    portable.OutputSize = msg.descriptor.OutputSize;
    return portable;
}

[[nodiscard]] bool should_skip_terminal_candidate(const CLSID &clsid) noexcept
{
    return clsid == clsid::zero || clsid == clsid::conhost;
}

[[nodiscard]] bool try_terminal_handoff_candidate(const CLSID &terminal_clsid, bool marker_check_required,
                                                  win32::handle_view server, win32::handle_view input_event,
                                                  const CONSOLE_PORTABLE_ATTACH_MSG &portable_msg, DWORD client_pid)
{
    LOG("deftermv2::try_terminal_handoff_candidate: clsid=%08X-%04X-%04X marker=%d pid=%lu", terminal_clsid.Data1,
        terminal_clsid.Data2, terminal_clsid.Data3, marker_check_required, client_pid);

    auto apartment = win32::com_apartment{COINIT_MULTITHREADED};

    com::com_ptr<IConsoleHandoff> handoff;
    try
    {
        handoff = com::create_instance<IConsoleHandoff>(terminal_clsid, CLSCTX_LOCAL_SERVER);
        LOG("deftermv2::try_terminal_handoff_candidate: create_instance ok ptr=%p", handoff.get());
    }
    catch (...)
    {
        LOG("deftermv2::try_terminal_handoff_candidate: create_instance failed");
        return false;
    }

    if (marker_check_required)
    {
        LOG("deftermv2::try_terminal_handoff_candidate: checking IDefaultTerminalMarker");
        (void)handoff.as<IDefaultTerminalMarker>();
        LOG("deftermv2::try_terminal_handoff_candidate: marker ok");
    }

    auto [signal_read, signal_write] = win32::create_pipe();
    auto corehost_process = win32::duplicate_self();
    win32::event terminal_process;

    LOG("deftermv2::try_terminal_handoff_candidate: EstablishHandoff server=%p event=%p signalWrite=%p self=%p "
        "id=%08lx:%08lx",
        server.get(), input_event.get(), signal_write.get(), corehost_process.get(), portable_msg.IdHighPart,
        portable_msg.IdLowPart);
    const auto hr = handoff->EstablishHandoff(server.get(), input_event.get(), &portable_msg, signal_write.get(),
                                              corehost_process.get(), terminal_process.put());
    LOG("deftermv2::try_terminal_handoff_candidate: EstablishHandoff hr=0x%08lx terminal=%p",
        static_cast<unsigned long>(hr), terminal_process.get());
    win32::throw_hresult(win32::hresult(hr));

    signal_write.clear();
    corehost_process.clear();

    auto shutdown_event = win32::event{win32::create_tag, true, false};
    auto signal_shutdown_event = win32::event{win32::duplicate_handle(shutdown_event.view())};
    auto thread_params = std::make_unique<defterm::signal_thread_params>(
        defterm::signal_thread_params{std::move(signal_read), std::move(signal_shutdown_event)});
    DWORD signal_thread_id = 0;
    auto signal_thread = win32::basic_thread{defterm::signal_thread_proc, thread_params.release(), &signal_thread_id};
    LOG("deftermv2::try_terminal_handoff_candidate: signal thread tid=%lu handle=%p shutdown=%p", signal_thread_id,
        signal_thread.get(), shutdown_event.get());

    std::array<HANDLE, 2> wait_handles{terminal_process.get(), shutdown_event.get()};
    LOG("deftermv2::try_terminal_handoff_candidate: waiting terminal=%p signalShutdown=%p", terminal_process.get(),
        shutdown_event.get());
    const auto wait_result =
        ::WaitForMultipleObjects(static_cast<DWORD>(wait_handles.size()), wait_handles.data(), FALSE, INFINITE);
    if (wait_result == WAIT_FAILED)
        win32::throw_last_error();

    LOG("deftermv2::try_terminal_handoff_candidate: wait result=%lu source=%ls", wait_result,
        wait_result == WAIT_OBJECT_0 ? L"process" : L"signal");
    return true;
}

[[nodiscard]] bool try_terminal_handoff(win32::handle_view server, win32::handle_view input_event,
                                        const CONSOLE_PORTABLE_ATTACH_MSG &portable_msg, DWORD client_pid)
{
    const auto candidates = std::array{
        clsid::default_clsid(clsid::delegation_step::console),
        clsid::wt_console,
        clsid::wt_console_pre,
        clsid::wt_console_can,
        clsid::wt_console_dev,
    };
    constexpr auto marker_required = std::array{false, true, true, true, true};

    for (size_t i = 0; i < candidates.size(); ++i)
    {
        if (should_skip_terminal_candidate(candidates[i]))
            continue;
        if (try_terminal_handoff_candidate(candidates[i], marker_required[i], server, input_event, portable_msg,
                                           client_pid))
            return true;
    }
    return false;
}

void dispatch_mini_fallback_message(win32::handle_view server, miniio::io_msg &msg, win32::handle &input,
                                    win32::handle &output)
{
    LOG("deftermv2::dispatch_mini_fallback_message: func=%ls(%lu) id=%08lx:%08lx pid=%llu object=%llu input=%p "
        "output=%p",
        io_function_name(msg.descriptor.Function), msg.descriptor.Function, msg.descriptor.Identifier.HighPart,
        msg.descriptor.Identifier.LowPart, static_cast<unsigned long long>(msg.descriptor.Process),
        static_cast<unsigned long long>(msg.descriptor.Object), input.get(), output.get());

    switch (msg.descriptor.Function)
    {
    case 0:
    case CONSOLE_IO_CONNECT:
        break;
    case CONSOLE_IO_DISCONNECT:
        input.clear();
        output.clear();
        miniio::prepare_completion(msg);
        break;
    case CONSOLE_IO_CREATE_OBJECT: {
        auto *request = reinterpret_cast<CD_CREATE_OBJECT_INFORMATION *>(msg.body);
        auto object_type = request->ObjectType;
        if (object_type == CD_IO_OBJECT_TYPE_GENERIC)
        {
            if ((request->DesiredAccess & (GENERIC_READ | GENERIC_WRITE)) == GENERIC_READ)
                object_type = CD_IO_OBJECT_TYPE_CURRENT_INPUT;
            else if ((request->DesiredAccess & (GENERIC_READ | GENERIC_WRITE)) == GENERIC_WRITE)
                object_type = CD_IO_OBJECT_TYPE_CURRENT_OUTPUT;
        }

        win32::handle new_handle;
        switch (object_type)
        {
        case CD_IO_OBJECT_TYPE_CURRENT_INPUT:
            new_handle = condrv::create_client_handle(server, L"\\Input");
            break;
        case CD_IO_OBJECT_TYPE_CURRENT_OUTPUT:
        case CD_IO_OBJECT_TYPE_NEW_OUTPUT:
            new_handle = condrv::create_client_handle(server, L"\\Output");
            break;
        default:
            miniio::prepare_completion(msg, 0xC0000001);
            return;
        }
        miniio::prepare_completion(msg, 0, reinterpret_cast<ULONG_PTR>(new_handle.release()));
        break;
    }
    case CONSOLE_IO_CLOSE_OBJECT:
        miniio::prepare_completion(msg);
        break;
    case CONSOLE_IO_RAW_WRITE:
        miniio::prepare_completion(msg, 0, msg.descriptor.InputSize);
        break;
    case CONSOLE_IO_RAW_READ:
        miniio::prepare_completion(msg);
        break;
    case CONSOLE_IO_USER_DEFINED:
        miniio::prepare_completion(msg, 0xC0000001);
        break;
    case CONSOLE_IO_RAW_FLUSH:
        miniio::prepare_completion(msg);
        break;
    default:
        std::unreachable();
    }
}

template <typename Handler>
void run_initial_connect_loop(win32::handle_view server, win32::handle_view input_event, Handler &handler)
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

        LOG("deftermv2::run_initial_connect_loop: got func=%ls(%lu) id=%08lx:%08lx pid=%llu object=%llu in=%lu out=%lu",
            io_function_name(current->descriptor.Function), current->descriptor.Function,
            current->descriptor.Identifier.HighPart, current->descriptor.Identifier.LowPart,
            static_cast<unsigned long long>(current->descriptor.Process),
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

    bridge.vt_append_str("\x1b[?9001h"sv);
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
            initial_connect = msg;
            has_initial_connect = true;
            width = connect_info.ScreenBufferSize.X > 0 ? connect_info.ScreenBufferSize.X : 0;
            height = connect_info.ScreenBufferSize.Y > 0 ? connect_info.ScreenBufferSize.Y : 0;
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

    assert(handler.has_initial_connect);
    win32::handle vt_in;
    win32::handle vt_out;
    win32::handle vt_in_keepalive;
    initialize_vt_handles(vt_in, vt_out, vt_in_keepalive);
    run_conpty(std::move(server), win32::handle{input_event.release()}, std::move(vt_in), std::move(vt_out),
               std::move(vt_in_keepalive), handler.initial_connect, handler.width, handler.height);
}

} // namespace deftermv2
