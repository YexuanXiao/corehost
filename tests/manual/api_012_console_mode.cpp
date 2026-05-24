// api_012_console_mode.cpp — 测试 GetConsoleMode
#include "manual_common.hpp"

int main()
{
    HANDLE hOut = init_console();
    title(L"api-012", L"GetConsoleMode — 控制台模式\n   测试 GetConsoleMode 读取输出和输入句柄的模式标志。\n   "
                      L"期望：显示输出模式（ENABLE_PROCESSED_OUTPUT、\n   "
                      L"ENABLE_WRAP_AT_EOL_OUTPUT、ENABLE_VIRTUAL_TERMINAL_PROCESSING、\n   "
                      L"DISABLE_NEWLINE_AUTO_RETURN）和输入模式（ENABLE_ECHO_INPUT、\n   "
                      L"ENABLE_LINE_INPUT、ENABLE_PROCESSED_INPUT 等）。\n   在 ConPTY 下 VT 处理应为启用状态。");

    wprint(L"\x1b[1;32m[期望结果]\x1b[0m\n");
    wprint(L"  输出模式应包含: ENABLE_VIRTUAL_TERMINAL_PROCESSING\n");
    wprint(L"  输入模式应包含: ENABLE_LINE_INPUT|ENABLE_PROCESSED_INPUT|ENABLE_ECHO_INPUT\n");

    sep();
    wprint(L"\x1b[1;32m[实际输出]\x1b[0m\n\n");

    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode;

    if (GetConsoleMode(hOut, &mode))
    {
        wprint(L"  输出模式: 0x%08lX\n", mode);
        wprint(L"    ENABLE_PROCESSED_OUTPUT:        %s\n", (mode & ENABLE_PROCESSED_OUTPUT) ? L"✓" : L"✗");
        wprint(L"    ENABLE_WRAP_AT_EOL_OUTPUT:      %s\n", (mode & ENABLE_WRAP_AT_EOL_OUTPUT) ? L"✓" : L"✗");
        wprint(L"    ENABLE_VIRTUAL_TERMINAL_PROCESSING: %s (应✓)\n",
               (mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) ? L"✓" : L"✗");
        wprint(L"    DISABLE_NEWLINE_AUTO_RETURN:    %s\n", (mode & DISABLE_NEWLINE_AUTO_RETURN) ? L"✓" : L"✗");
        wprint(L"    ENABLE_LVB_GRID_WORLDWIDE:      %s\n", (mode & ENABLE_LVB_GRID_WORLDWIDE) ? L"✓" : L"✗");
    }

    if (GetConsoleMode(hIn, &mode))
    {
        wprint(L"\n  输入模式: 0x%08lX\n", mode);
        wprint(L"    ENABLE_ECHO_INPUT:              %s\n", (mode & ENABLE_ECHO_INPUT) ? L"✓" : L"✗");
        wprint(L"    ENABLE_LINE_INPUT:              %s\n", (mode & ENABLE_LINE_INPUT) ? L"✓" : L"✗");
        wprint(L"    ENABLE_PROCESSED_INPUT:         %s\n", (mode & ENABLE_PROCESSED_INPUT) ? L"✓" : L"✗");
        wprint(L"    ENABLE_VIRTUAL_TERMINAL_INPUT:  %s\n", (mode & ENABLE_VIRTUAL_TERMINAL_INPUT) ? L"✓" : L"✗");
        wprint(L"    ENABLE_WINDOW_INPUT:            %s\n", (mode & ENABLE_WINDOW_INPUT) ? L"✓" : L"✗");
    }

    wait3s(L"检查模式标志：VT处理应为 ✓");
    return 0;
}
