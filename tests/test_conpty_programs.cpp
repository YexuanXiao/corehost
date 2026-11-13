// tests/test_conpty_programs.cpp
// ConPTY 下的真实程序用例：cmd、嵌套子进程、powershell、vim，以及输入时延。
//
// 设计见 doc/plan/04-conpty-tests.md：每用例独立会话、条件+截止时间等待、
// 外部程序缺失时 SKIP 而不是假装通过。
#include "lib/conpty_session.hpp"
#include "lib/ssh_server.hpp"
#include "lib/vt_screen.hpp"
#include "lib/platform.hpp"
#include "lib/runner.hpp"

#include <chrono>
#include <string>
#include <cstdio>
#include <fstream>
#include <algorithm>
#include <vector>

namespace
{

std::wstring quote(std::wstring_view path)
{
    return L"\"" + std::wstring{path} + L"\"";
}

corehost::test::result cmd_echo_and_exit()
{
    const auto cmd = corehost::test::system32_path(L"cmd.exe");
    REQUIRE(!cmd.empty());

    corehost::test::conpty_session session;
    REQUIRE(session.start(quote(cmd) + L" /c \"echo cmd-marker\""));

    REQUIRE(session.wait_output("cmd-marker", 15000));

    DWORD exit_code = static_cast<DWORD>(-1);
    REQUIRE(session.wait_exit(15000, &exit_code));
    REQUIRE(exit_code == 0);
    return corehost::test::result::pass;
}

// 控制台由 cmd 再拉起一个 cmd：子进程必须继承同一个控制台才能输出。
corehost::test::result nested_child_inherits_console()
{
    const auto cmd = corehost::test::system32_path(L"cmd.exe");
    REQUIRE(!cmd.empty());

    corehost::test::conpty_session session;
    REQUIRE(session.start(quote(cmd) + L" /c \"cmd /c echo nested-marker\""));

    REQUIRE(session.wait_output("nested-marker", 15000));

    DWORD exit_code = static_cast<DWORD>(-1);
    REQUIRE(session.wait_exit(15000, &exit_code));
    REQUIRE(exit_code == 0);
    return corehost::test::result::pass;
}

corehost::test::result powershell_command()
{
    const auto powershell = corehost::test::system32_path(L"WindowsPowerShell\\v1.0\\powershell.exe");
    if (!corehost::test::file_exists(powershell))
        SKIP(L"powershell.exe not found");

    corehost::test::conpty_session session;
    REQUIRE(session.start(quote(powershell) + L" -NoLogo -NoProfile -Command \"Write-Output ps-marker\""));

    REQUIRE(session.wait_output("ps-marker", 20000));

    DWORD exit_code = static_cast<DWORD>(-1);
    REQUIRE(session.wait_exit(20000, &exit_code));
    REQUIRE(exit_code == 0);
    return corehost::test::result::pass;
}

corehost::test::result vim_starts_and_quits()
{
    const auto vim = corehost::test::find_on_path(L"vim.exe");
    if (vim.empty())
        SKIP(L"vim.exe not on PATH");

    corehost::test::conpty_session session;
    // -i NONE 禁用 viminfo；CI 用户目录不可写时 vim 会停在
    // "E138: Can't write viminfo" 提示上，导致 :qa! 等待用户按 Enter。
    const std::wstring command = quote(vim) + L" -u NONE -N -n -i NONE";
    if (!session.start(command))
    {
        std::fwprintf(stderr, L"[vim] session.start failed, command=%ls\n", command.c_str());
        REQUIRE(false);
    }

    // 不同构建/vim 版本的终端初始化序列不固定；启动后直接送 normal-mode
    // 的 ex 命令，不依赖 1049 备用屏幕标记。原始 ESC 在 VT 输入流里是序列
    // 前缀，单独写可能被 parser 挂起而无法成为 Vim 的 Esc。
    ::Sleep(1000);
    if (!session.write(":qa!\r"))
    {
        std::fwprintf(stderr, L"[vim] session.write failed, running=%d\n", session.is_running() ? 1 : 0);
        REQUIRE(false);
    }

    DWORD exit_code = static_cast<DWORD>(-1);
    const bool exited = session.wait_exit(20000, &exit_code);
    if (!exited || exit_code != 0)
    {
        const auto output = session.take_output();
        std::fwprintf(stderr, L"[vim] exited=%d code=%lu running=%d, output tail: %hs\n",
                      exited ? 1 : 0, static_cast<unsigned long>(exit_code), session.is_running() ? 1 : 0,
                      output.size() > 1200 ? output.c_str() + output.size() - 1200 : output.c_str());
    }
    REQUIRE(exited);
    REQUIRE(exit_code == 0);
    return corehost::test::result::pass;
}

// A10：输入到回显的时延（不轮询模型的回归指标）。
corehost::test::result input_latency()
{
    // 探针已确认回显链路工作（cmd /k 输入 abc 后收到 \r\nabc\r\n）。但连续
    // 交互式 cmd 会话在隔离子进程里会提前结束，等待标记随之失败；稳定化前
    // 保持 SKIP，见 doc/tasks/04-open-gaps.md G12。

    const auto cmd = corehost::test::system32_path(L"cmd.exe");
    REQUIRE(!cmd.empty());

    corehost::test::conpty_session session;
    REQUIRE(session.start(quote(cmd)));

    // 等提示符出现，确保会话已就绪。
    REQUIRE(session.wait_output(">", 15000));
    (void)session.take_output();

    std::vector<long long> samples;
    samples.reserve(20);

    for (int i = 0; i < 20; ++i)
    {
        (void)session.take_output();
        const auto marker = "zzmark" + std::to_string(i);
        const auto start = std::chrono::steady_clock::now();
        REQUIRE(session.write(marker + "\r"));
        REQUIRE(session.wait_output(marker, 5000));
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
        samples.push_back(static_cast<long long>(elapsed.count()));
    }

    session.write("\x03");
    std::sort(samples.begin(), samples.end());
    const auto p95 = samples[samples.size() * 95 / 100];
    std::fwprintf(stdout, L"[latency] p95=%lldms max=%lldms ", p95, samples.back());

    REQUIRE(p95 < 250);
    return corehost::test::result::pass;
}

corehost::test::result edit_help_runs()
{
    const auto edit = corehost::test::system32_path(L"edit.exe");
    if (!corehost::test::file_exists(edit))
        SKIP(L"edit.exe not found");

    corehost::test::conpty_session session;
    REQUIRE(session.start(quote(edit) + L" /?"));

    REQUIRE(session.wait_output("Usage: edit", 20000));

    DWORD exit_code = static_cast<DWORD>(-1);
    REQUIRE(session.wait_exit(20000, &exit_code));
    REQUIRE(exit_code == 0);
    return corehost::test::result::pass;
}

// ssh 远程组合：本机 sshd + ssh -tt 远程命令。
// 前置：sshd 服务已运行（需要管理员启动）且当前用户已配置密钥登录。
corehost::test::result ssh_remote_command()
{
    const auto ssh = corehost::test::system32_path(L"OpenSSH\\ssh.exe");
    if (!corehost::test::file_exists(ssh))
        SKIP(L"ssh.exe not found");

    // 用测试进程自己拉起的临时 sshd（当前用户、高端口、密钥认证），
    // 不依赖系统 sshd 服务，也不需要管理员权限。
    corehost::test::ssh_server server;
    std::wstring error;
    if (!server.start(error))
        SKIP((L"cannot start temporary sshd: " + error).c_str());

    corehost::test::conpty_session session;
    const std::wstring command =
        quote(ssh) + L" " + server.client_options() + L" " + server.user() + L"@127.0.0.1 \"cmd /c echo ssh-marker\"";
    REQUIRE(session.start(command));

    const bool saw_marker = session.wait_output("ssh-marker", 60000);

    DWORD exit_code = static_cast<DWORD>(-1);
    const bool exited = session.wait_exit(30000, &exit_code);

    if (!saw_marker)
    {
        const auto output = session.take_output();
        std::fwprintf(stderr, L"[ssh] session output: %hs\n", output.c_str());
    }

    server.stop();
    REQUIRE(saw_marker);
    REQUIRE(exited);
    REQUIRE(exit_code == 0);
    return corehost::test::result::pass;
}

// show/hide 上报：向伪窗口发 WM_SIZE(MINIMIZED/RESTORED)，宿主应向 vt_out
// 写 ESC[2t / ESC[1t（与原版 VtIo::Writer::WriteWindowVisibility 一致）。
corehost::test::result visibility_is_reported_to_terminal()
{
    const auto cmd = corehost::test::system32_path(L"cmd.exe");
    REQUIRE(!cmd.empty());

    corehost::test::conpty_session session;
    REQUIRE(session.start(quote(cmd) + L" /c \"echo READY & for /l %i in (1,1,100000) do @rem\""));
    REQUIRE(session.wait_output("READY", 5000));

    DWORD host_pid = 0;
    for (int attempt = 0; attempt < 100 && host_pid == 0; ++attempt)
    {
        host_pid = corehost::test::find_child_process_id(L"corehost.exe");
        if (host_pid == 0)
            ::Sleep(20);
    }
    REQUIRE(host_pid != 0);

    HWND window = nullptr;
    for (int attempt = 0; attempt < 100 && window == nullptr; ++attempt)
    {
        window = corehost::test::find_top_level_window(host_pid, L"PseudoConsoleWindow");
        if (window == nullptr)
            ::Sleep(20);
    }
    REQUIRE(window != nullptr);

    (void)session.take_output();

    DWORD_PTR result = 0;
    REQUIRE(::SendMessageTimeoutW(window, WM_SIZE, SIZE_MINIMIZED, 0, SMTO_ABORTIFHUNG, 2000, &result) != 0);
    REQUIRE(session.wait_output("\x1b[2t", 5000));

    (void)session.take_output();
    REQUIRE(::SendMessageTimeoutW(window, WM_SIZE, SIZE_RESTORED, 0, SMTO_ABORTIFHUNG, 2000, &result) != 0);
    REQUIRE(session.wait_output("\x1b[1t", 5000));

    return corehost::test::result::pass;
}

// ssh 交互会话：请求 PTY、进入远端 shell，再通过终端输入一条命令。
// 这条用例覆盖"键盘输入经 ssh 转发到远端"的完整链路。
corehost::test::result ssh_interactive_session()
{
    const auto ssh = corehost::test::system32_path(L"OpenSSH\\ssh.exe");
    if (!corehost::test::file_exists(ssh))
        SKIP(L"ssh.exe not found");

    corehost::test::ssh_server server;
    std::wstring error;
    if (!server.start(error))
        SKIP((L"cannot start temporary sshd: " + error).c_str());

    corehost::test::conpty_session session;
    const std::wstring command = quote(ssh) + L" -tt " + server.client_options() + L" " + server.user() + L"@127.0.0.1";
    REQUIRE(session.start(command));

    // 等远端 shell 起来（cmd.exe 的提示符），再输入命令。
    const bool ready = session.wait_output(">", 60000);
    if (!ready)
    {
        const auto output = session.take_output();
        std::fwprintf(stderr, L"[ssh-pty] no prompt, output: %hs\n", output.c_str());
    }
    REQUIRE(ready);

    REQUIRE(session.write("echo ssh-pty-marker\r"));
    const bool saw_marker = session.wait_output("ssh-pty-marker", 30000);

    if (!saw_marker)
    {
        const auto output = session.take_output();
        std::fwprintf(stderr, L"[ssh-pty] missing marker, output: %hs\n", output.c_str());
    }

    server.stop();
    REQUIRE(saw_marker);
    return corehost::test::result::pass;
}

// ssh 负路径 1：客户端密钥未授权 → 认证失败（非 0 退出 + Permission denied）。
corehost::test::result ssh_rejects_unknown_key()
{
    const auto ssh = corehost::test::system32_path(L"OpenSSH\\ssh.exe");
    if (!corehost::test::file_exists(ssh))
        SKIP(L"ssh.exe not found");

    corehost::test::ssh_server server;
    std::wstring error;
    if (!server.start(error))
        SKIP((L"cannot start temporary sshd: " + error).c_str());

    corehost::test::conpty_session session;
    // 只用未授权的 hostkey，构造独立的选项串（client_options 里已含 -i 客户端密钥，
    // 再加 -i 会让 ssh 同时尝试合法密钥而认证成功）。
    const std::wstring command = quote(ssh) + L" -p " + std::to_wstring(server.port()) + L" -i \"" + server.host_key() +
                                 L"\" -o IdentitiesOnly=yes -o IdentityAgent=NUL" +
                                 L" -o StrictHostKeyChecking=no -o UserKnownHostsFile=NUL -o BatchMode=yes" +
                                 L" -o LogLevel=ERROR " + server.user() + L"@127.0.0.1 \"cmd /c echo should-not-run\"";
    REQUIRE(session.start(command));

    const bool saw = session.wait_output("Permission denied", 30000);
    DWORD exit_code = static_cast<DWORD>(-1);
    const bool exited = session.wait_exit(30000, &exit_code);
    if (!saw)
    {
        const auto output = session.take_output();
        std::fprintf(stderr, "[ssh-bad-key] output: %s\n", output.c_str());
    }
    server.stop();
    REQUIRE(saw);
    REQUIRE(exited);
    REQUIRE(exit_code != 0);
    return corehost::test::result::pass;
}

// ssh 负路径 2：远端命令的退出码应传播回 ssh。
corehost::test::result ssh_propagates_remote_exit_code()
{
    const auto ssh = corehost::test::system32_path(L"OpenSSH\\ssh.exe");
    if (!corehost::test::file_exists(ssh))
        SKIP(L"ssh.exe not found");

    corehost::test::ssh_server server;
    std::wstring error;
    if (!server.start(error))
        SKIP((L"cannot start temporary sshd: " + error).c_str());

    corehost::test::conpty_session session;
    const std::wstring command =
        quote(ssh) + L" " + server.client_options() + L" " + server.user() + L"@127.0.0.1 \"exit 7\"";
    REQUIRE(session.start(command));

    DWORD exit_code = static_cast<DWORD>(-1);
    const bool exited = session.wait_exit(30000, &exit_code);
    if (!exited || exit_code != 7)
    {
        const auto output = session.take_output();
        std::fprintf(stderr, "[ssh-exit] exited=%d code=%lu output: %s\n", exited ? 1 : 0,
                     static_cast<unsigned long>(exit_code), output.c_str());
    }
    server.stop();
    REQUIRE(exited);
    REQUIRE(exit_code == 7);
    return corehost::test::result::pass;
}

// ssh 负路径 3：交互会话里把 ^C 转发到远端，远端命令被中断、ssh 退出。
corehost::test::result ssh_interactive_interrupt()
{
    const auto ssh = corehost::test::system32_path(L"OpenSSH\\ssh.exe");
    if (!corehost::test::file_exists(ssh))
        SKIP(L"ssh.exe not found");

    corehost::test::ssh_server server;
    std::wstring error;
    if (!server.start(error))
        SKIP((L"cannot start temporary sshd: " + error).c_str());

    corehost::test::conpty_session session;
    const std::wstring command = quote(ssh) + L" -tt " + server.client_options() + L" " + server.user() +
                                 L"@127.0.0.1 \"powershell -NoProfile -Command Start-Sleep -Seconds 30\"";
    REQUIRE(session.start(command));

    // 等 ssh 建立会话并进入 raw 模式，再把 ^C 作为字节发给远端。重复发送覆盖
    // "字节早于 raw read 挂起"的窗口。
    ::Sleep(1500);
    bool exited = false;
    for (int attempt = 0; attempt < 10 && !exited; ++attempt)
    {
        if (!session.is_running())
        {
            exited = true;
            break;
        }
        if (!session.write("\x03"))
        {
            std::fprintf(stderr, "[ssh-interrupt] write failed at attempt %d, running=%d\n", attempt,
                         session.is_running() ? 1 : 0);
            break;
        }
        exited = session.wait_exit_poll(1000);
    }
    if (!exited)
        exited = session.wait_exit(20000);
    if (!exited)
    {
        const auto output = session.take_output();
        std::fprintf(stderr, "[ssh-interrupt] not exited, running=%d, output: %s\n",
                     session.is_running() ? 1 : 0, output.c_str());
    }
    server.stop();
    REQUIRE(exited);
    return corehost::test::result::pass;
}

// cls（ED 2）之后旧文本必须从渲染屏幕上消失，新文本可见。
corehost::test::result screen_erase_removes_old_text()
{
    const auto cmd = corehost::test::system32_path(L"cmd.exe");
    REQUIRE(!cmd.empty());

    corehost::test::conpty_session session;
    REQUIRE(session.start(quote(cmd) + L" /c \"echo AAA & cls & echo BBB\""));
    REQUIRE(session.wait_output("BBB", 15000));

    corehost::test::vt_screen screen{80, 25};
    screen.feed(session.take_output());
    const bool saw_bbb = screen.contains("BBB");
    const bool saw_aaa = screen.contains("AAA");

    DWORD exit_code = static_cast<DWORD>(-1);
    (void)session.wait_exit(15000, &exit_code);
    session.close();
    if (!saw_bbb || saw_aaa)
        std::fprintf(stderr, "[screen] bbb=%d aaa=%d\n", saw_bbb ? 1 : 0, saw_aaa ? 1 : 0);
    REQUIRE(saw_bbb);
    REQUIRE(!saw_aaa);
    return corehost::test::result::pass;
}

// 脚本化客户端：API 序列（写文本/读 SBI/mode/标题）。
corehost::test::result script_console_api()
{
    const auto probe = corehost::test::quote_self_dir(L"console_probe.exe");
    REQUIRE(!probe.empty());

    wchar_t module[MAX_PATH]{};
    REQUIRE(::GetModuleFileNameW(nullptr, module, ARRAYSIZE(module)) != 0);
    const auto script = (std::filesystem::path{module}.parent_path() / L"script_api.txt");
    {
        std::ofstream file{script, std::ios::binary};
        file << "write hello\nsbi\ngetmode\nsettitle script-title\ngettitle\nexit 0\n";
    }

    corehost::test::conpty_session session;
    REQUIRE(session.start(quote(probe) + L" --script \"" + script.wstring() + L"\""));
    REQUIRE(session.wait_output("[script] gettitle", 15000));
    DWORD exit_code = static_cast<DWORD>(-1);
    REQUIRE(session.wait_exit(15000, &exit_code));
    const auto output = session.take_output();
    session.close();
    std::filesystem::remove(script);

    if (output.find("[script] write ok") == std::string::npos || output.find("sbi size=") == std::string::npos ||
        output.find("[script] gettitle \"script-title\"") == std::string::npos)
        std::fprintf(stderr, "[script_api] output: %s\n", output.c_str());
    REQUIRE(output.find("[script] write ok") != std::string::npos);
    REQUIRE(output.find("sbi size=") != std::string::npos);
    REQUIRE(output.find("[script] gettitle \"script-title\"") != std::string::npos);
    REQUIRE(exit_code == 0);
    return corehost::test::result::pass;
}

// 脚本化客户端：窗口消息（标题长度/内容、show 不影响可见性）。
corehost::test::result script_window_messages()
{
    const auto probe = corehost::test::quote_self_dir(L"console_probe.exe");
    REQUIRE(!probe.empty());

    wchar_t module[MAX_PATH]{};
    REQUIRE(::GetModuleFileNameW(nullptr, module, ARRAYSIZE(module)) != 0);
    const auto script = (std::filesystem::path{module}.parent_path() / L"script_window.txt");
    {
        std::ofstream file{script, std::ios::binary};
        file << "settitle script-window\nwindow\nmsg gettextlength\nmsg gettext\nmsg showon\nwindow\n"
                "msg minimize\nmsg restore\nexit 0\n";
    }

    corehost::test::conpty_session session;
    REQUIRE(session.start(quote(probe) + L" --script \"" + script.wstring() + L"\""));
    REQUIRE(session.wait_output("[script] msg restore", 15000));
    DWORD exit_code = static_cast<DWORD>(-1);
    REQUIRE(session.wait_exit(15000, &exit_code));
    const auto output = session.take_output();
    session.close();
    std::filesystem::remove(script);

    if (output.find("[script] window valid=1 visible=0") == std::string::npos ||
        output.find("[script] msg gettextlength result=13") == std::string::npos ||
        output.find("[script] msg gettext \"script-window\"") == std::string::npos)
        std::fprintf(stderr, "[script_window] output: %s\n", output.c_str());
    REQUIRE(output.find("[script] window valid=1 visible=0") != std::string::npos);
    REQUIRE(output.find("[script] msg gettextlength result=13") != std::string::npos);
    REQUIRE(output.find("[script] msg gettext \"script-window\"") != std::string::npos);
    REQUIRE(exit_code == 0);
    return corehost::test::result::pass;
}

// 脚本化客户端：写文本后由测试用 VT 屏幕模型断言（ReadConsoleOutputCharacter
// 在 ConPTY 下会让客户端崩溃，见 gaps G28，因此不在探针里回读）。
corehost::test::result script_expect_screen_text()
{
    const auto probe = corehost::test::quote_self_dir(L"console_probe.exe");
    REQUIRE(!probe.empty());

    wchar_t module[MAX_PATH]{};
    REQUIRE(::GetModuleFileNameW(nullptr, module, ARRAYSIZE(module)) != 0);
    const auto script = (std::filesystem::path{module}.parent_path() / L"script_expect.txt");
    {
        std::ofstream file{script, std::ios::binary};
        file << "write EXPECT-TEXT\nsleep 300\nexpect EXPECT-TEXT\nexit 0\n";
    }

    corehost::test::conpty_session session;
    REQUIRE(session.start(quote(probe) + L" --script \"" + script.wstring() + L"\""));
    REQUIRE(session.wait_output("[script] expect", 15000));
    DWORD exit_code = static_cast<DWORD>(-1);
    REQUIRE(session.wait_exit(15000, &exit_code));
    const auto output = session.take_output();
    session.close();
    std::filesystem::remove(script);

    // 既验证 ReadConsoleOutputCharacter 回读（expect），也验证 VT 屏幕模型。
    corehost::test::vt_screen screen{80, 25};
    screen.feed(output);
    if (output.find("[script] expect \"EXPECT-TEXT\" ok=1") == std::string::npos)
        std::fprintf(stderr, "[script_expect] output: %s\n", output.c_str());
    REQUIRE(output.find("[script] expect \"EXPECT-TEXT\" ok=1") != std::string::npos);
    REQUIRE(screen.contains("EXPECT-TEXT"));
    REQUIRE(exit_code == 0);
    return corehost::test::result::pass;
}

} // namespace

int main()
{
    constexpr corehost::test::test_case cases[] = {
        {"cmd", "conpty;program", &cmd_echo_and_exit},
        {"screen_erase", "conpty;program;screen", &screen_erase_removes_old_text},
        {"script_api", "conpty;program;script", &script_console_api},
        {"script_window", "conpty;program;script", &script_window_messages},
        {"script_expect", "conpty;program;script", &script_expect_screen_text},
        {"nested", "conpty;program;inherit", &nested_child_inherits_console},
        {"powershell", "conpty;program", &powershell_command},
        {"vim", "conpty;program;interactive", &vim_starts_and_quits},
        {"latency", "conpty;perf", &input_latency},
        {"edit", "conpty;program", &edit_help_runs},
        {"ssh", "conpty;program;network", &ssh_remote_command},
        {"ssh_pty", "conpty;program;network", &ssh_interactive_session},
        {"ssh_bad_key", "conpty;program;network", &ssh_rejects_unknown_key},
        {"ssh_exit_code", "conpty;program;network", &ssh_propagates_remote_exit_code},
        {"ssh_interrupt", "conpty;program;network", &ssh_interactive_interrupt},
        {"visibility", "conpty;window", &visibility_is_reported_to_terminal},
    };

    if (const auto child = corehost::test::run_child_case(cases))
        return *child;

    const auto stats = corehost::test::run_isolated(cases, {}, 120000);
    std::fwprintf(stdout, L"=== conpty programs: %d passed, %d failed, %d skipped ===\n", stats.passed, stats.failed,
                  stats.skipped);
    return corehost::test::exit_code(stats);
}