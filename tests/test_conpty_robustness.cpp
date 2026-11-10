// tests/test_conpty_robustness.cpp
// ConPTY 健壮性用例：resize、clear、非法尺寸、退出码传播、重复会话。
#include "lib/conpty_session.hpp"
#include "lib/platform.hpp"
#include "lib/runner.hpp"

#include <cstdio>

namespace
{

std::wstring quote(std::wstring_view path)
{
    return L"\"" + std::wstring{path} + L"\"";
}

// resize 之后客户端看到的新尺寸（用 `mode con` 读取，不需要额外探针程序）。
corehost::test::result resize_changes_client_size()
{
    const auto cmd = corehost::test::system32_path(L"cmd.exe");
    REQUIRE(!cmd.empty());

    corehost::test::conpty_session session;
    REQUIRE(session.start(quote(cmd)));
    REQUIRE(session.wait_output(">", 15000));
    (void)session.take_output();

    REQUIRE(session.resize(COORD{100, 30}));

    // 尺寸通过 PTY 信号到达会话后，mode con 才会报告新值；轮询等待而不是固定睡眠。
    REQUIRE(session.write("mode con\r"));

    bool saw_width = false;
    bool saw_height = false;
    for (int attempt = 0; attempt < 30 && !(saw_width && saw_height); ++attempt)
    {
        const auto output = session.take_output();
        if (output.find("100") != std::string::npos)
            saw_width = true;
        if (output.find("30") != std::string::npos)
            saw_height = true;
        if (!(saw_width && saw_height))
            ::Sleep(100);
    }

    session.write("exit\r");
    (void)session.wait_exit(10000);

    REQUIRE(saw_width);
    REQUIRE(saw_height);
    return corehost::test::result::pass;
}

// clear 之后会话仍可继续工作（不崩溃、后续命令正常）。
corehost::test::result clear_keeps_session_usable()
{
    const auto cmd = corehost::test::system32_path(L"cmd.exe");
    REQUIRE(!cmd.empty());

    corehost::test::conpty_session session;
    REQUIRE(session.start(quote(cmd)));
    REQUIRE(session.wait_output(">", 15000));

    REQUIRE(session.clear(true));
    (void)session.take_output();

    REQUIRE(session.write("echo after-clear\r"));
    REQUIRE(session.wait_output("after-clear", 15000));

    session.write("exit\r");
    (void)session.wait_exit(10000);
    return corehost::test::result::pass;
}

// 非法的伪控制台尺寸必须被拒绝，而不是创建出坏会话。
corehost::test::result invalid_size_is_rejected()
{
    HPCON pty = nullptr;
    HANDLE input_read = nullptr;
    HANDLE input_write = nullptr;
    HANDLE output_read = nullptr;
    HANDLE output_write = nullptr;
    SECURITY_ATTRIBUTES attributes{sizeof(attributes), nullptr, TRUE};

    REQUIRE(::CreatePipe(&input_read, &input_write, &attributes, 0) != FALSE);
    REQUIRE(::CreatePipe(&output_read, &output_write, &attributes, 0) != FALSE);

    const HRESULT hr = ConptyCreatePseudoConsole(COORD{0, 0}, input_read, output_write, 0, &pty);

    ::CloseHandle(input_read);
    ::CloseHandle(input_write);
    ::CloseHandle(output_read);
    ::CloseHandle(output_write);

    REQUIRE(pty == nullptr);
    REQUIRE(FAILED(hr));
    return corehost::test::result::pass;
}

// 客户端退出码必须原样传回。
corehost::test::result exit_code_is_propagated()
{
    const auto cmd = corehost::test::system32_path(L"cmd.exe");
    REQUIRE(!cmd.empty());

    corehost::test::conpty_session session;
    REQUIRE(session.start(quote(cmd) + L" /c \"exit 7\""));

    DWORD exit_code = static_cast<DWORD>(-1);
    REQUIRE(session.wait_exit(15000, &exit_code));
    REQUIRE(exit_code == 7);
    return corehost::test::result::pass;
}

// 连续 20 次会话：每次都干净退出，测试进程自身句柄不泄漏。
corehost::test::result repeated_sessions()
{
    const auto cmd = corehost::test::system32_path(L"cmd.exe");
    REQUIRE(!cmd.empty());

    DWORD handles_before = 0;
    REQUIRE(::GetProcessHandleCount(::GetCurrentProcess(), &handles_before) != FALSE);

    for (int iteration = 0; iteration < 20; ++iteration)
    {
        corehost::test::conpty_session session;
        REQUIRE(session.start(quote(cmd) + L" /c \"echo repeat-marker\""));
        REQUIRE(session.wait_output("repeat-marker", 15000));

        DWORD exit_code = static_cast<DWORD>(-1);
        REQUIRE(session.wait_exit(15000, &exit_code));
        REQUIRE(exit_code == 0);
    }

    DWORD handles_after = 0;
    REQUIRE(::GetProcessHandleCount(::GetCurrentProcess(), &handles_after) != FALSE);

    std::fwprintf(stdout, L"[repeat] handles %lu -> %lu ", handles_before, handles_after);
    // 每次会话允许少量临时对象；20 次后不应线性增长。
    REQUIRE(handles_after <= handles_before + 40);
    return corehost::test::result::pass;
}

} // namespace

int main()
{
    constexpr corehost::test::test_case cases[] = {
        {"resize", "conpty;robust", &resize_changes_client_size},
        {"clear", "conpty;robust", &clear_keeps_session_usable},
        {"invalid_size", "conpty;robust", &invalid_size_is_rejected},
        {"exit_code", "conpty;robust", &exit_code_is_propagated},
        {"repeat", "conpty;robust;stability", &repeated_sessions},
    };

    if (const auto child = corehost::test::run_child_case(cases))
        return *child;

    const auto stats = corehost::test::run_isolated(cases, {}, 120000);
    std::fwprintf(stdout, L"=== conpty robustness: %d passed, %d failed, %d skipped ===\n", stats.passed, stats.failed,
                  stats.skipped);
    return corehost::test::exit_code(stats);
}
