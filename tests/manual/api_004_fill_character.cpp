// api_004_fill_character.cpp — 测试 FillConsoleOutputCharacter
#include "manual_common.hpp"

int main()
{
    HANDLE hOut = init_console();
    title(L"api-004",
          L"FillConsoleOutputCharacter — 填充字符\n   测试 FillConsoleOutputCharacterW 和 "
          L"FillConsoleOutputCharacterA。\n   期望：第一行从光标位置开始填充 30 个 '#'；\n   第二行从光标位置开始填充 "
          L"20 个 '*'。\n   填充位置应精确从指定坐标开始，数量应恰好为 30 和 20。");

    wprint(L"\x1b[1;32m[期望结果]\x1b[0m\n");
    wprint(L"  第1行: 从光标开始连续 30 个 '#' 字符\n");
    wprint(L"  第2行: 从光标开始连续 20 个 '*' 字符\n");

    sep();
    wprint(L"\x1b[1;32m[实际输出]\x1b[0m\n\n");

    CONSOLE_SCREEN_BUFFER_INFO csbi{};
    GetConsoleScreenBufferInfo(hOut, &csbi);
    COORD start = csbi.dwCursorPosition;
    DWORD filled;

    FillConsoleOutputCharacterW(hOut, L'#', 30, start, &filled);
    wprint(L"  FillConsoleOutputCharacterW('#', 30, pos=(%d,%d)) → 实际填充 %lu 字符\n", start.X, start.Y, filled);
    start.Y += 1;
    SetConsoleCursorPosition(hOut, start);

    FillConsoleOutputCharacterA(hOut, '*', 20, start, &filled);
    wprint(L"  FillConsoleOutputCharacterA('*', 20, pos=(%d,%d)) → 实际填充 %lu 字符\n", start.X, start.Y, filled);

    start.Y += 1;
    start.X = 0;
    SetConsoleCursorPosition(hOut, start);
    wprint(L"\n  \x1b[1;37m验证:\x1b[0m 上方两行应分别为 30个# 和 20个*\n");

    wait3s(L"检查上方两行：# 行 30 个, * 行 20 个");
    return 0;
}
