// api_008_scroll.cpp — 测试 ScrollConsoleScreenBuffer
#include "manual_common.hpp"

int main()
{
    HANDLE hOut = init_console();
    title(L"api-008", L"ScrollConsoleScreenBuffer — 滚动屏幕缓冲区\n   测试 ScrollConsoleScreenBufferW "
                      L"向上滚动指定矩形区域的 2 行。\n   期望：先在当前光标位置下方写 5 行 A-E，\n   然后将这 5 "
                      L"行向上滚动 2 行，下方 2 行用空格填充。\n   滚动后 A B C D E 应整体上移 2 行。");

    wprint(L"\x1b[1;32m[期望结果]\x1b[0m\n");
    wprint(L"  滚动前: 行0=A×30, 行1=B×30, 行2=C×30, 行3=D×30, 行4=E×30\n");
    wprint(L"  滚动后: 行(-2)=A, 行(-1)=B, 行0=C, 行1=D, 行2=E, 行3..4=空格\n");
    wprint(L"  即 A-E 整体向上移动了 2 行\n");

    sep();
    wprint(L"\x1b[1;32m[实际输出]\x1b[0m\n\n");

    CONSOLE_SCREEN_BUFFER_INFO csbi{};
    GetConsoleScreenBufferInfo(hOut, &csbi);
    SHORT row = csbi.dwCursorPosition.Y;

    // 先写 5 行
    for (int i = 0; i < 5; ++i)
    {
        COORD p = {0, static_cast<SHORT>(row + i)};
        wchar_t ch = static_cast<wchar_t>(L'A' + i);
        FillConsoleOutputCharacterW(hOut, ch, 30, p, nullptr);
    }
    csbi.dwCursorPosition.Y = row + 5;
    SetConsoleCursorPosition(hOut, csbi.dwCursorPosition);
    wprint(L"  已写 5 行 (A-E 各 30 个), 向上滚动 2 行...\n");
    Sleep(1500);

    SMALL_RECT sr = {0, row, csbi.dwSize.X - 1, static_cast<SHORT>(row + 4)};
    COORD dest = {0, static_cast<SHORT>(row - 2)};
    CHAR_INFO fill{};
    fill.Char.UnicodeChar = L' ';
    fill.Attributes = csbi.wAttributes;
    ScrollConsoleScreenBufferW(hOut, &sr, nullptr, dest, &fill);

    csbi.dwCursorPosition.Y = row + 5;
    SetConsoleCursorPosition(hOut, csbi.dwCursorPosition);
    wprint(L"  滚动完成: 矩形 (%d,%d)-(%d,%d) → dest (%d,%d)\n", sr.Left, sr.Top, sr.Right, sr.Bottom, dest.X, dest.Y);
    wprint(L"\n  \x1b[1;37m验证:\x1b[0m A-E 应整体上移了 2 行\n");

    wait3s(L"检查 A-E 是否上移 2 行");
    return 0;
}
