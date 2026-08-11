// ── cli/main.cxx ────────────────────────────────────────
// corehost 入口点
//

#define WIN32_NO_STATUS
#include <windows.h>
#undef WIN32_NO_STATUS

#include "console_arguments.hpp"
#include "defterm/defterm.hpp"
#include "comserver/com_server.hpp"
#include "conpty.hpp"
#include "client/client.hpp"
#include "condrv_io.hpp"
#include "win32/error.hpp"
#include "win32/com_apartment.hpp"
#include "win32/event.hpp"
#include "utility/crtdbg.hpp"
#include "utility/log.hpp"
#include "shell/shell.hpp"
#include "win32/debugging.hpp"

int __stdcall wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
try
{
    utility::suppress_crt_error_dialogs();
    corehost::log::initialize_log();
    LOG("corehost process start, cmdline=%ls", ::GetCommandLineW());
    auto args = corehost::cli::console_arguments{::GetCommandLineW()};

    if (args.com_server())
    {
        LOG("entering com_server_entry");
        auto hr = corehost::comserver::com_server_entry();
        LOG("com_server_entry returned: server=%p vt_in=%p vt_out=%p event=%p signal=%p w=%d h=%d", hr.server.get(),
            hr.vt_in.get(), hr.vt_out.get(), hr.event.get(), hr.signal.get(), hr.width, hr.height);

        LOG("entering conpty_entry");
        corehost::conpty::conpty_entry(hr.server, hr.event, std::move(hr.condrv_input), std::move(hr.condrv_output),
                                       hr.vt_in, hr.vt_out, std::move(hr.signal), hr.width, hr.height, false,
                                       corehost::conpty::text_measurement_mode::graphemes, true);
        LOG("conpty_entry returned cleanly");
        return 0;
    }

    // 如果第一个参数是句柄，那么开始默认终端握手
    if (auto ch = args.condrv_handle(); ch != 0)
    {
        LOG("entering defterm_entry, handle=0x%Ix", ch);
        corehost::defterm::defterm_entry(ch);
        LOG("defterm_entry returned");
        return 0;
    }

    // todo:
    // https://github.com/microsoft/terminal/blob/694db4c0bc6e506db51ad98937db6ea062ab8a00/src/interactivity/win32/windowio.cpp#L927
    if (args.is_headless())
    {
        LOG("entering conpty (--headless) mode, server=0x%Ix", args.server_handle());

        auto server = win32::handle::from_uintptr(args.server_handle());
        auto input_event = win32::event{win32::create_tag, true, false};
        auto ev = win32::handle{input_event.release()};
        corehost::condrv_io::set_server_info(server.view(), ev.view());

        // 如果有--signal <handle>, 绑定为会话信号管道（主循环轮询）
        win32::handle sig_pipe;
        if (auto sh = args.signal_handle(); sh != 0)
            sig_pipe = win32::handle::from_uintptr(sh);

        LOG("entering conpty_entry (mode=%d ambiguous=%d)", static_cast<int>(args.text_measurement()),
            args.ambiguous_is_wide());
        corehost::conpty::conpty_entry(server, ev, {}, {}, win32::handle_view{::GetStdHandle(STD_INPUT_HANDLE)},
                                       win32::handle_view{::GetStdHandle(STD_OUTPUT_HANDLE)}, std::move(sig_pipe),
                                       args.width(), args.height(), args.inherit_cursor(), args.text_measurement(),
                                       args.ambiguous_is_wide());
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
        corehost::client::client_entry({shell_info.name.data(), shell_info.name.size()}, std::move(shell_info.path));
    }
    else
    {
        corehost::client::client_entry({}, std::wstring(cmdline.data(), cmdline.size()));
    }
    return 0;
}
catch (...)
{
    LOG("unhandled exception in wWinMain");
    return 1;
}
