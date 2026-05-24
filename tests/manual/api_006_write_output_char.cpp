// api_006_write_output_char.cpp — 测试 WriteConsoleOutputCharacter
#include "manual_common.hpp"

int main()
{
    HANDLE hOut = init_console();
    title(L"api-006",
          L"WriteConsoleOutputCharacter — 指定坐标写字符\n   测试在指定坐标写入字符（不移动光标）。\n   "
          L"期望：在当前光标行第 0 列写入 11 个中文字符\n   'WriteConsoleOutputCharacter 写的中文'，光标位置不变。");

    wprint(L"\x1b[1;32m[期望结果]\x1b[0m\n");
    wprint(L"  上方一行 (光标所在行) 显示 11 个中文字符\n");
    wprint(L"  'WriteConsoleOutputCharacter 写的中文'\n");
    wprint(L"  注意：WriteConsoleOutputCharacter 不移动光标\n");

    sep();
    wprint(L"\x1b[1;32m[实际输出]\x1b[0m\n\n");

    CONSOLE_SCREEN_BUFFER_INFO csbi{};
    GetConsoleScreenBufferInfo(hOut, &csbi);
    COORD writePos = {0, csbi.dwCursorPosition.Y};
    DWORD written;

    const wchar_t *str = L"WriteConsoleOutputCharacter 写的中文";
    DWORD len = static_cast<DWORD>(wcslen(str));
    WriteConsoleOutputCharacterW(hOut, str, len, writePos, &written);
    wprint(L"  WriteConsoleOutputCharacterW 写入 %lu 字符\n", written);
    wprint(L"  位置: (%d, %d), 内容: %s\n", writePos.X, writePos.Y, str);

    writePos.Y += 2;
    writePos.X = 0;
    SetConsoleCursorPosition(hOut, writePos);
    wprint(L"\n  \x1b[1;37m验证:\x1b[0m 上方应显示上述中文, 光标应在本行说明文字处\n");

    wait3s(L"检查上方中文输出是否正确, 光标是否在原位");
    return 0;
}
