// ── cli/main.cxx ────────────────────────────────────────
// corehost 入口点
//

#define WIN32_NO_STATUS
#include <windows.h>
#undef WIN32_NO_STATUS

#include "console_arguments.hpp"
#include "defterm/defterm.hpp"
#include "deftermv2/deftermv2.hpp"
#include "comserver/com_server.hpp"
#include "conpty.hpp"
#include "client/client.hpp"
#include "miniio/io_thread.hpp"
#include "win32/error.hpp"
#include "win32/com_apartment.hpp"
#include "utility/crtdbg.hpp"
#include "utility/log.hpp"
#include "shell/shell.hpp"
#include "ntapi/consolecontrol.hpp"
#include "ntapi/consolenslmode.hpp"
#include "win32/debugging.hpp"

int __stdcall wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
try
{
    utility::suppress_crt_error_dialogs();
    console::initialize_console_control();
    console::initialize_console_nls();
    LOG("corehost process start, cmdline=%ls", ::GetCommandLineW());
    auto args = console::console_arguments{::GetCommandLineW()};

    if (args.com_server())
    {
        LOG("entering com_server_entry");
        auto hr = comserver::com_server_entry();
        LOG("com_server_entry returned: server=%p vt_in=%p vt_out=%p event=%p signal=%p w=%d h=%d", hr.server.get(),
            hr.vt_in.get(), hr.vt_out.get(), hr.event.get(), hr.signal.get(), hr.width, hr.height);

        LOG("entering conpty_entry");
        conpty::conpty_entry(std::move(hr.server), std::move(hr.event), std::move(hr.condrv_input),
                             std::move(hr.condrv_output), std::move(hr.vt_in), std::move(hr.vt_out),
                             std::move(hr.signal), hr.width, hr.height, false, conpty::text_measurement_mode::graphemes,
                             true);
        LOG("conpty_entry returned cleanly");
        return 0;
    }

    // 如果第一个参数是句柄，那么开始默认终端握手
    if (auto ch = args.condrv_handle(); ch != 0)
    {
        LOG("entering deftermv2_entry, handle=0x%Ix", ch);
        auto hr = deftermv2::deftermv2_entry(ch);
        LOG("deftermv2_entry returned");
        // 由于缺少默认终端或者运行在提升后进程而无法启动
        if (!hr.server.valid())
            return 0;
        LOG("entering conpty_entry from deftermv2: server=%p vt_in=%p vt_out=%p event=%p signal=%p w=%d h=%d",
            hr.server.get(), hr.vt_in.get(), hr.vt_out.get(), hr.event.get(), hr.signal.get(), hr.width,
            hr.height);
        conpty::conpty_entry(std::move(hr.server), std::move(hr.event), std::move(hr.condrv_input),
                             std::move(hr.condrv_output), std::move(hr.vt_in), std::move(hr.vt_out),
                             std::move(hr.signal), hr.width, hr.height, false,
                             conpty::text_measurement_mode::graphemes, true);
        LOG("conpty_entry from deftermv2 returned cleanly");
        return 0;
    }

    if (args.is_headless())
    {
        LOG("entering conpty (--headless) mode, server=0x%Ix", args.server_handle());

        // 对标原始: ConsoleServerInitialization →ConsoleCreateIoThread
        auto server = win32::handle{reinterpret_cast<HANDLE>(args.server_handle())};
        auto ev = win32::handle{::CreateEventW(nullptr, TRUE, FALSE, nullptr)};
        if (!ev.valid())
            win32::throw_last_error();
        miniio::set_server_info(server.view(), ev.view());

        // 如果有--signal <handle>, 传递给信号线程
        win32::handle sig_pipe;
        if (auto sh = args.signal_handle(); sh != 0)
            sig_pipe = win32::handle{reinterpret_cast<HANDLE>(sh)};

        LOG("entering conpty_entry (mode=%d ambiguous=%d)", static_cast<int>(args.text_measurement()),
            args.ambiguous_is_wide());
        conpty::conpty_entry(std::move(server), std::move(ev), {}, {}, win32::handle{::GetStdHandle(STD_INPUT_HANDLE)},
                             win32::handle{::GetStdHandle(STD_OUTPUT_HANDLE)}, std::move(sig_pipe), args.width(),
                             args.height(), args.inherit_cursor(), args.text_measurement(), args.ambiguous_is_wide());
        LOG("conpty_entry returned cleanly");
        return 0;
    }

    // ── client 模式 ──────────────────────────────────────
    // 不是 com_server / headless / defterm →解析为客户端命令行
    auto cmdline = args.client_command_line();

    // 如果命令为空，则启动 pwsh/powershell/cmd

    if (cmdline.empty())
    {
        auto shell_info = shell::get_shell();
        client::client_entry({shell_info.name.data(), shell_info.name.size()}, std::move(shell_info.path));
    }
    else
    {
        client::client_entry({}, std::wstring(cmdline.data(), cmdline.size()));
    }
    return 0;
}
catch (...)
{
    LOG("unhandled exception in wWinMain");
    return 1;
}
