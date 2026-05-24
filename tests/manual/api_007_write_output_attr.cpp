// api_007_write_output_attr.cpp — 测试 WriteConsoleOutputAttribute
#include "manual_common.hpp"

int main()
{
    HANDLE hOut = init_console();
    title(L"api-007", L"WriteConsoleOutputAttribute — 指定坐标写颜色属性\n   测试 WriteConsoleOutputAttribute "
                      L"在指定位置写入颜色属性（不改变字符）。\n   期望：在当前行填充 30 个 'C' 字符，然后分成 6 段各 "
                      L"5 个字符，\n   分别设置红、绿、蓝、黄、品、青六种前景色。颜色应精确对应。");

    wprint(L"\x1b[1;32m[期望结果]\x1b[0m\n");
    wprint(L"  一行 30 个 'C', 分 6 段各 5 个:\n");
    wprint(L"    C=0..4:  红色 (FOREGROUND_RED)\n");
    wprint(L"    C=5..9:  绿色 (FOREGROUND_GREEN)\n");
    wprint(L"    C=10..14: 蓝色 (FOREGROUND_BLUE)\n");
    wprint(L"    C=15..19: 黄色 (RED+GREEN)\n");
    wprint(L"    C=20..24: 品色 (RED+BLUE)\n");
    wprint(L"    C=25..29: 青色 (GREEN+BLUE)\n");

    sep();
    wprint(L"\x1b[1;32m[实际输出]\x1b[0m\n\n");

    CONSOLE_SCREEN_BUFFER_INFO csbi{};
    GetConsoleScreenBufferInfo(hOut, &csbi);
    COORD writePos = csbi.dwCursorPosition;

    FillConsoleOutputCharacterW(hOut, L'C', 30, writePos, nullptr);

    WORD attrs[] = {
        FOREGROUND_RED,
        FOREGROUND_GREEN,
        FOREGROUND_BLUE,
        FOREGROUND_RED | FOREGROUND_GREEN,
        FOREGROUND_RED | FOREGROUND_BLUE,
        FOREGROUND_GREEN | FOREGROUND_BLUE,
    };
    const wchar_t *names[] = {L"红", L"绿", L"蓝", L"黄", L"品", L"青"};
    for (int i = 0; i < 6; ++i)
    {
        COORD p = {static_cast<SHORT>(writePos.X + i * 5), writePos.Y};
        DWORD n;
        WriteConsoleOutputAttribute(hOut, &attrs[i], 5, p, &n);
        wprint(L"  [%d] 位置(%d,%d) 颜色=%s 写入%lu\n", i, p.X, p.Y, names[i], n);
    }

    writePos.Y += 2;
    writePos.X = 0;
    SetConsoleCursorPosition(hOut, writePos);
    wprint(L"\n  \x1b[1;37m验证:\x1b[0m 上方一行 30 个 C, 每 5 个一种颜色\n");

    wait3s(L"检查六色：红绿蓝黄品青 各5个C");
    return 0;
}
