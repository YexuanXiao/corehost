#pragma once

#include <windows.h>
#include <array>
#include <memory>
#include <objbase.h>
#include <ranges>
#include "defterm/signal.hpp"
#include "IConsoleHandoff.h"
#include "com/clsid.hpp"
#include "com/com_ptr.hpp"
#include "miniio/io_thread.hpp"
#include "utility/log.hpp"
#include "win32/com_apartment.hpp"
#include "win32/error.hpp"
#include "win32/event.hpp"
#include "win32/handle.hpp"
#include "win32/hresult.hpp"
#include "win32/thread.hpp"

namespace deftermv2
{

[[nodiscard]] inline CONSOLE_PORTABLE_ATTACH_MSG make_portable_attach_msg(const miniio::io_msg &msg) noexcept
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

[[nodiscard]] inline bool should_skip_terminal(const CLSID &clsid) noexcept
{
    return clsid == clsid::zero || clsid == clsid::conhost;
}

[[nodiscard]] inline bool try_terminal_handoff(const CLSID &terminal_clsid, bool marker_check_required,
                                               win32::handle_view server, win32::handle_view input_event,
                                               const CONSOLE_PORTABLE_ATTACH_MSG &portable_msg, DWORD client_pid)
{
    LOG("deftermv2::try_terminal_handoff: clsid=%08X-%04X-%04X marker=%d pid=%lu", terminal_clsid.Data1,
        terminal_clsid.Data2, terminal_clsid.Data3, marker_check_required, client_pid);

    auto apartment = win32::com_apartment{COINIT_MULTITHREADED};

    com::com_ptr<IConsoleHandoff> handoff;
    try
    {
        handoff = com::create_instance<IConsoleHandoff>(terminal_clsid, CLSCTX_LOCAL_SERVER);
        LOG("deftermv2::try_terminal_handoff: create_instance ok ptr=%p", handoff.get());
    }
    catch (...)
    {
        LOG("deftermv2::try_terminal_handoff: create_instance failed");
        return false;
    }

    if (marker_check_required)
    {
        LOG("deftermv2::try_terminal_handoff: checking IDefaultTerminalMarker");
        (void)handoff.as<IDefaultTerminalMarker>();
        LOG("deftermv2::try_terminal_handoff: marker ok");
    }

    auto [signal_read, signal_write] = win32::create_pipe();
    auto corehost_process = win32::duplicate_self();
    win32::event terminal_process;

    LOG("deftermv2::try_terminal_handoff: EstablishHandoff server=%p event=%p signalWrite=%p self=%p "
        "id=%08lx:%08lx",
        server.get(), input_event.get(), signal_write.get(), corehost_process.get(), portable_msg.IdHighPart,
        portable_msg.IdLowPart);
    const auto hr = handoff->EstablishHandoff(server.get(), input_event.get(), &portable_msg, signal_write.get(),
                                              corehost_process.get(), terminal_process.put());
    LOG("deftermv2::try_terminal_handoff: EstablishHandoff hr=0x%08lx terminal=%p", static_cast<unsigned long>(hr),
        terminal_process.get());
    win32::throw_hresult(win32::hresult(hr));

    signal_write.clear();
    corehost_process.clear();

    auto shutdown_event = win32::event{win32::create_tag, true, false};
    auto signal_shutdown_event = win32::event{win32::duplicate_handle(shutdown_event.view())};
    auto thread_params = std::make_unique<defterm::signal_thread_params>(
        defterm::signal_thread_params{std::move(signal_read), std::move(signal_shutdown_event)});
    DWORD signal_thread_id = 0;
    auto signal_thread = win32::basic_thread{defterm::signal_thread_proc, thread_params.release(), &signal_thread_id};
    LOG("deftermv2::try_terminal_handoff: signal thread tid=%lu handle=%p shutdown=%p", signal_thread_id,
        signal_thread.get(), shutdown_event.get());

    std::array<HANDLE, 2> wait_handles{terminal_process.get(), shutdown_event.get()};
    LOG("deftermv2::try_terminal_handoff: waiting terminal=%p signalShutdown=%p", terminal_process.get(),
        shutdown_event.get());
    const auto wait_result =
        ::WaitForMultipleObjects(static_cast<DWORD>(wait_handles.size()), wait_handles.data(), FALSE, INFINITE);
    if (wait_result == WAIT_FAILED)
        win32::throw_last_error();

    LOG("deftermv2::try_terminal_handoff: wait result=%lu source=%ls", wait_result,
        wait_result == WAIT_OBJECT_0 ? L"process" : L"signal");
    return true;
}

[[nodiscard]] inline bool try_terminal_handoff(win32::handle_view server, win32::handle_view input_event,
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

    for (const auto [candidate, marker_check_required] : std::views::zip(candidates, marker_required))
    {
        if (should_skip_terminal(candidate))
            continue;
        if (try_terminal_handoff(candidate, marker_check_required, server, input_event, portable_msg, client_pid))
            return true;
    }
    return false;
}

} // namespace deftermv2
