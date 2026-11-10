// tests/console_probe.cpp
// 测试用控制台探针：在被测 ConPTY 会话里以"真实客户端"身份运行，用于验证
// 输入模式相关行为（Ctrl+C 投递）与进程状态。
//
// 用法：
//   console_probe --ctrl-probe    保持 ENABLE_PROCESSED_INPUT 并长眠，等待
//                                 Ctrl+C 由 CSRSS 结束（默认处理器退出码
//                                 0xC000013A）。启动后输出 READY 标记。
#include <windows.h>

#include <cstdio>
#include <cstring>

int wmain(int argc, wchar_t **argv)
{
    if (argc != 2 || ::wcscmp(argv[1], L"--ctrl-probe") != 0)
    {
        ::fwprintf(stderr, L"usage: console_probe --ctrl-probe\n");
        return 2;
    }

    // 关键：显式要求 processed input，Ctrl+C 才会被转成 CTRL_C_EVENT。
    DWORD mode = 0;
    if (::GetConsoleMode(::GetStdHandle(STD_INPUT_HANDLE), &mode) == FALSE)
        return 3;
    mode |= ENABLE_PROCESSED_INPUT | ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT;
    if (::SetConsoleMode(::GetStdHandle(STD_INPUT_HANDLE), mode) == FALSE)
        return 4;

    constexpr char ready[] = "CTRL-PROBE-READY\r\n";
    DWORD written = 0;
    ::WriteFile(::GetStdHandle(STD_OUTPUT_HANDLE), ready, sizeof(ready) - 1, &written, nullptr);

    // 不再读输入：如果 Ctrl+C 被正确投递，进程会被 CSRSS 结束。
    ::Sleep(120000);
    return 0;
}
