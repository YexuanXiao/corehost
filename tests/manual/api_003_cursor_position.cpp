// api_003_cursor_position.cpp — 测试 SetConsoleCursorPosition
#include "manual_common.hpp"

int main()
{
    HANDLE hOut = init_console();
    title(L"api-003", L"SetConsoleCursorPosition — 光标定位\n   测试移动光标到指定坐标。\n   期望：光标先移动到 (10, "
                      L"当前行)，再移动到 (30, 当前行)，\n   最后移动到 (0, 下一行)。光标应精确出现在指定列和行。");

    wprint(L"\x1b[1;32m[期望结果]\x1b[0m\n");
    wprint(L"  位置1: (10, Y) — 光标在第 10 列\n");
    wprint(L"  位置2: (30, Y) — 光标在第 30 列\n");
    wprint(L"  位置3: (0, Y+1) — 光标在下一行第 0 列\n");

    sep();
    wprint(L"\x1b[1;32m[实际输出]\x1b[0m\n\n");

    CONSOLE_SCREEN_BUFFER_INFO csbi{};
    GetConsoleScreenBufferInfo(hOut, &csbi);

    wprint(L"  [1] 移动光标到 (10, %d): ", csbi.dwCursorPosition.Y);
    COORD pos = {10, csbi.dwCursorPosition.Y};
    SetConsoleCursorPosition(hOut, pos);
    wprint(L"OK\n");
    Sleep(800);

    wprint(L"  [2] 移动光标到 (30, %d): ", csbi.dwCursorPosition.Y);
    GetConsoleScreenBufferInfo(hOut, &csbi);
    pos = {30, csbi.dwCursorPosition.Y};
    SetConsoleCursorPosition(hOut, pos);
    wprint(L"OK\n");
    Sleep(800);

    wprint(L"  [3] 移动光标到 (0, %d): ", csbi.dwCursorPosition.Y + 1);
    GetConsoleScreenBufferInfo(hOut, &csbi);
    pos = {0, static_cast<SHORT>(csbi.dwCursorPosition.Y + 1)};
    SetConsoleCursorPosition(hOut, pos);
    wprint(L"OK → 光标应在第 0 列, 上一行的下一行\n");

    wait3s(L"观察光标位置变化：10→30→0 列, 最后换行");
    return 0;
}
