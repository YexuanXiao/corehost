// api_005_fill_attribute.cpp — 测试 FillConsoleOutputAttribute
#include "manual_common.hpp"

int main()
{
    HANDLE hOut = init_console();
    title(L"api-005", L"FillConsoleOutputAttribute — 填充颜色属性\n   测试在指定位置填充颜色属性，不改变字符。\n   "
                      L"期望：先填充 40 个 'X' 作为载体，然后用 FillConsoleOutputAttribute\n   "
                      L"分别设置红底白字(10个)、绿底白字(10个)、蓝底白字(10个)、品红底白字(10个)。\n   "
                      L"每种颜色的位置应精确，数量应为 10。");

    wprint(L"\x1b[1;32m[期望结果]\x1b[0m\n");
    wprint(L"  一行 40 个 'X', 分 4 段:\n");
    wprint(L"    段1 (X=0..9):   红底白字 (BACKGROUND_RED)\n");
    wprint(L"    段2 (X=10..19): 绿底白字 (BACKGROUND_GREEN)\n");
    wprint(L"    段3 (X=20..29): 蓝底白字 (BACKGROUND_BLUE)\n");
    wprint(L"    段4 (X=30..39): 品红底白字 (BACKGROUND_RED|BACKGROUND_BLUE)\n");

    sep();
    wprint(L"\x1b[1;32m[实际输出]\x1b[0m\n\n");

    CONSOLE_SCREEN_BUFFER_INFO csbi{};
    GetConsoleScreenBufferInfo(hOut, &csbi);
    COORD start = csbi.dwCursorPosition;

    FillConsoleOutputCharacterW(hOut, L'X', 40, start, nullptr);
    wprint(L"  已写入 40 个 'X', 下面染色:\n");

    FillConsoleOutputAttribute(hOut, BACKGROUND_RED | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE, 10, start,
                               nullptr);
    start.X += 10;
    FillConsoleOutputAttribute(hOut, BACKGROUND_GREEN | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE, 10, start,
                               nullptr);
    start.X += 10;
    FillConsoleOutputAttribute(hOut, BACKGROUND_BLUE | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE, 10, start,
                               nullptr);
    start.X += 10;
    FillConsoleOutputAttribute(hOut,
                               BACKGROUND_RED | BACKGROUND_BLUE | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE,
                               10, start, nullptr);

    csbi.dwCursorPosition.Y += 2;
    csbi.dwCursorPosition.X = 0;
    SetConsoleCursorPosition(hOut, csbi.dwCursorPosition);
    wprint(L"\n  \x1b[1;37m验证:\x1b[0m 上方 40 个 X 分四色：红底/绿底/蓝底/品红底\n");

    wait3s(L"检查四种颜色块：红底→绿底→蓝底→品红底 各10个X");
    return 0;
}
