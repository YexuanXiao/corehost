// api_015_read_console.cpp — 测试 ReadConsole
#include "manual_common.hpp"

int main()
{
    HANDLE hOut = init_console();
    title(L"api-015",
          L"ReadConsole — 交互式输入测试\n   测试 ReadConsoleW 读取用户输入。\n   期望：程序提示输入，用户键入文字后按 "
          L"Enter，\n   程序回显读取的字符数和内容（应包含 \\r\\n）。\n   回显内容应与用户输入一致。");

    wprint(L"\x1b[1;32m[期望结果]\x1b[0m\n");
    wprint(L"  输入后 ReadConsole 返回:\n");
    wprint(L"    - 字符数 = 输入字符数 + 2 (含 \\r\\n)\n");
    wprint(L"    - 内容 = 用户输入 + \\r\\n\n");

    sep();
    wprint(L"\x1b[1;32m[实际输出]\x1b[0m\n\n");

    wprint(L"  请输入一段文字 (以 Enter 结束):\n  >>> ");
    wchar_t buf[256]{};
    DWORD read;
    ReadConsoleW(GetStdHandle(STD_INPUT_HANDLE), buf, 255, &read, nullptr);
    wprint(L"\n  ReadConsole 返回 %lu 字符\n", read);
    wprint(L"  内容 (含 \\r\\n): [");
    WriteConsoleW(hOut, buf, read, &read, nullptr);
    wprint(L"]\n");

    wprint(L"\n  \x1b[1;37m验证:\x1b[0m 方括号内的内容应与你输入完全一致\n");

    wait3s(L"检查回显是否与输入一致");
    return 0;
}
