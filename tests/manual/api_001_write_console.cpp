// api_001_write_console.cpp — 测试 WriteConsole 基本文本输出
#include "manual_common.hpp"

int main()
{
    HANDLE hOut = init_console();
    title(L"api-001", L"WriteConsole — 基本文本输出\n   测试 WriteConsoleW 写入宽字符串、WriteConsoleA 写入 ANSI "
                      L"字符串。\n   期望：第一行输出 'Hello, ConPTY!'，第二行输出 'Hello from ANSI!'，\n   第三行输出 "
                      L"80 个 '=' 填满整行，之后输出三行带编号的文字。\n   所有文本应正确显示，无乱码，无多余空格。");

    wprint(L"\x1b[1;32m[期望结果]\x1b[0m\n");
    wprint(L"  第1行: Hello, ConPTY!\n");
    wprint(L"  第2行: Hello from ANSI!\n");
    wprint(L"  第3行: 80 个 '=' 字符, 恰好填满一行\n");
    wprint(L"  第4-6行: '第一行' '第二行' '第三行' 各占一行\n");

    sep();
    wprint(L"\x1b[1;32m[实际输出]\x1b[0m\n\n");

    // WriteConsoleW
    const wchar_t *text = L"Hello, ConPTY!\r\n";
    DWORD written;
    WriteConsoleW(hOut, text, static_cast<DWORD>(wcslen(text)), &written, nullptr);

    // WriteConsoleA
    const char *ansi_text = "Hello from ANSI!\r\n";
    WriteConsoleA(hOut, ansi_text, static_cast<DWORD>(strlen(ansi_text)), &written, nullptr);

    // 长文本 80 个 =
    wchar_t line[81]{};
    for (int i = 0; i < 80; ++i)
        line[i] = L'=';
    WriteConsoleW(hOut, line, 80, &written, nullptr);
    wprint(L"\n");

    // 多行
    WriteConsoleW(hOut, L"第一行\r\n第二行\r\n第三行\r\n",
                  static_cast<DWORD>(wcslen(L"第一行\r\n第二行\r\n第三行\r\n")), &written, nullptr);

    wait3s(L"检查上方输出：Hello/ANSI/80个=/三行中文");
    return 0;
}
