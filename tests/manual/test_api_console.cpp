// test_manual_api.cpp — 控制台 API 功能手动验证程序
// 用法: 在 ConPTY (corehost.exe) 下运行, 观察每个 API 是否工作正常
// 编译: cl /EHsc /std:c++17 /utf-8 test_manual_api.cpp /Fe:test_api.exe

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

// ── 工具: 写入宽字符串到控制台 ──
void wprint(const wchar_t *fmt, ...)
{
    wchar_t buf[4096];
    va_list args;
    va_start(args, fmt);
    int n = _vsnwprintf_s(buf, _TRUNCATE, fmt, args);
    va_end(args);
    if (n > 0)
    {
        DWORD written;
        WriteConsoleW(GetStdHandle(STD_OUTPUT_HANDLE), buf, static_cast<DWORD>(n), &written, nullptr);
    }
}

// ── 工具: 写标题分隔线 ──
void section(const wchar_t *title)
{
    wprint(L"\n══════════════════════════════════════════════════\n");
    wprint(L"  %s\n", title);
    wprint(L"══════════════════════════════════════════════════\n\n");
}

// ── 工具: 给用户时间观察 ──
void pause_and_continue(const wchar_t *hint)
{
    wprint(L"\n  >>> %s — 按 Enter 继续...", hint);
    char buf[8];
    DWORD rd;
    ReadFile(GetStdHandle(STD_INPUT_HANDLE), buf, 1, &rd, nullptr);
    // 吃掉回车
    while (rd > 0)
    {
        ReadFile(GetStdHandle(STD_INPUT_HANDLE), buf, 1, &rd, nullptr);
        if (buf[0] == '\n')
            break;
    }
    wprint(L"\n");
}

// ═══════════════════════════════════════════════════════
// 测试 1: WriteConsole — 基本文本输出
// ═══════════════════════════════════════════════════════
void test_write_console()
{
    section(L"1. WriteConsole — 文本输出");

    wprint(L"  WriteConsoleW 写入 'Hello, ConPTY!':\n  >>> ");
    const wchar_t *text = L"Hello, ConPTY!\r\n";
    DWORD written;
    WriteConsoleW(GetStdHandle(STD_OUTPUT_HANDLE), text, static_cast<DWORD>(wcslen(text)), &written, nullptr);

    wprint(L"  WriteConsoleA 写入 ANSI 文本:\n  >>> ");
    const char *ansi_text = "Hello from ANSI!\r\n";
    WriteConsoleA(GetStdHandle(STD_OUTPUT_HANDLE), ansi_text, static_cast<DWORD>(strlen(ansi_text)), &written, nullptr);

    wprint(L"  WriteConsoleW 写入长文本 (80 列线):\n  >>> ");
    wchar_t line[81]{};
    for (int i = 0; i < 80; ++i)
        line[i] = L'=';
    WriteConsoleW(GetStdHandle(STD_OUTPUT_HANDLE), line, 80, &written, nullptr);
    wprint(L"\n");

    wprint(L"  WriteConsoleW 带 CR/LF 的多行:\n");
    WriteConsoleW(GetStdHandle(STD_OUTPUT_HANDLE), L"  第一行\r\n  第二行\r\n  第三行\r\n",
                  static_cast<DWORD>(wcslen(L"  第一行\r\n  第二行\r\n  第三行\r\n")), &written, nullptr);

    pause_and_continue(L"观察上面输出是否正确 (Hello/ANSI/分隔线/三行文字)");
}

// ═══════════════════════════════════════════════════════
// 测试 2: GetConsoleScreenBufferInfo — 屏幕缓冲区信息
// ═══════════════════════════════════════════════════════
void test_screen_buffer_info()
{
    section(L"2. GetConsoleScreenBufferInfo — 屏幕缓冲区信息");

    CONSOLE_SCREEN_BUFFER_INFO csbi{};
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (GetConsoleScreenBufferInfo(hOut, &csbi))
    {
        wprint(L"  dwSize:              (%d, %d)  ← 缓冲区宽×高\n", csbi.dwSize.X, csbi.dwSize.Y);
        wprint(L"  dwCursorPosition:    (%d, %d)  ← 当前光标位置 (0-based)\n", csbi.dwCursorPosition.X,
               csbi.dwCursorPosition.Y);
        wprint(L"  wAttributes:         0x%04X   ← 当前文本属性\n", csbi.wAttributes);
        wprint(L"  srWindow:            (%d,%d)-(%d,%d)  ← 可视窗口\n", csbi.srWindow.Left, csbi.srWindow.Top,
               csbi.srWindow.Right, csbi.srWindow.Bottom);
        wprint(L"  dwMaximumWindowSize: (%d, %d)  ← 最大窗口尺寸\n", csbi.dwMaximumWindowSize.X,
               csbi.dwMaximumWindowSize.Y);
    }
    else
        wprint(L"  ERROR: GetConsoleScreenBufferInfo 失败! GLE=%lu\n", GetLastError());

    pause_and_continue(L"检查窗口大小和光标位置是否正确");
}

// ═══════════════════════════════════════════════════════
// 测试 3: SetConsoleCursorPosition — 光标定位
// ═══════════════════════════════════════════════════════
void test_cursor_position()
{
    section(L"3. SetConsoleCursorPosition — 光标定位");

    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);

    wprint(L"  移动光标到 (10, 当前行): ");
    CONSOLE_SCREEN_BUFFER_INFO csbi{};
    GetConsoleScreenBufferInfo(hOut, &csbi);
    COORD pos = {10, csbi.dwCursorPosition.Y};
    SetConsoleCursorPosition(hOut, pos);
    wprint(L"OK\n");

    wprint(L"  移动光标到 (30, 当前行): ");
    GetConsoleScreenBufferInfo(hOut, &csbi);
    pos = {30, csbi.dwCursorPosition.Y};
    SetConsoleCursorPosition(hOut, pos);
    wprint(L"OK → 看光标是否跳到了 (30, 当前行)\n");

    wprint(L"  移动光标到 (0, 下一行): ");
    GetConsoleScreenBufferInfo(hOut, &csbi);
    pos = {0, static_cast<SHORT>(csbi.dwCursorPosition.Y + 1)};
    SetConsoleCursorPosition(hOut, pos);
    wprint(L"OK → 光标是否在最左?  Y 是否+1?\n");

    pause_and_continue(L"检查光标移动是否正确");
}

// ═══════════════════════════════════════════════════════
// 测试 4: FillConsoleOutputCharacter — 填充字符
// ═══════════════════════════════════════════════════════
void test_fill_character()
{
    section(L"4. FillConsoleOutputCharacter — 填充字符");

    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi{};
    GetConsoleScreenBufferInfo(hOut, &csbi);

    COORD start = csbi.dwCursorPosition;
    DWORD filled;
    wprint(L"  FillConsoleOutputCharacterW 填充 30 个 '#' (从光标位置):\n  ");
    FillConsoleOutputCharacterW(hOut, L'#', 30, start, &filled);
    start.Y += 1;
    SetConsoleCursorPosition(hOut, start);
    wprint(L"\n  已填充 %lu 字符, 见上一行\n", filled);

    wprint(L"  FillConsoleOutputCharacterA 填充 20 个 '*' (从光标位置):\n  ");
    FillConsoleOutputCharacterA(hOut, '*', 20, start, &filled);
    start.Y += 1;
    SetConsoleCursorPosition(hOut, start);
    wprint(L"\n  已填充 %lu 字符, 见上一行\n", filled);

    pause_and_continue(L"检查两行填充 (30个# 和 20个*) 是否正确");
}

// ═══════════════════════════════════════════════════════
// 测试 5: FillConsoleOutputAttribute — 填充属性 (颜色)
// ═══════════════════════════════════════════════════════
void test_fill_attribute()
{
    section(L"5. FillConsoleOutputAttribute — 填充颜色属性");

    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi{};
    GetConsoleScreenBufferInfo(hOut, &csbi);

    // 先输出一些字符
    COORD start = csbi.dwCursorPosition;

    // 要持续检查 start 合法性——如果最接近的是 +15 行，
    // 我们刚在前面 test_fill_character 中加了换行，
    // 所以 start.Y 后面几行是没关系的,
    // 但又不能截得太准确。稳妥一点：限制在 sb 高度内。
    FillConsoleOutputCharacterW(hOut, L'X', 40, start, nullptr);
    wprint(L"背景字符已写入 (40个 'X'), 下面染色:\n");

    // 前10个 红底白字
    FillConsoleOutputAttribute(hOut, BACKGROUND_RED | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE, 10, start,
                               nullptr);

    // 中10个 绿底白字
    start.X += 10;
    FillConsoleOutputAttribute(hOut, BACKGROUND_GREEN | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE, 10, start,
                               nullptr);

    // 后10个 蓝底白字
    start.X += 10;
    FillConsoleOutputAttribute(hOut, BACKGROUND_BLUE | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE, 10, start,
                               nullptr);

    // 最终 10 个 品红底白字
    start.X += 10;
    FillConsoleOutputAttribute(hOut,
                               BACKGROUND_RED | BACKGROUND_BLUE | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE,
                               10, start, nullptr);

    csbi.dwCursorPosition.Y += 2;
    csbi.dwCursorPosition.X = 0;
    SetConsoleCursorPosition(hOut, csbi.dwCursorPosition);
    wprint(L"  上一行应为: 红底白字 | 绿底白字 | 蓝底白字 | 品红底白字 (各10个 X)\n");

    pause_and_continue(L"检查四种颜色块是否正确");
}

// ═══════════════════════════════════════════════════════
// 测试 6: WriteConsoleOutputCharacter — 指定位置写字符
// ═══════════════════════════════════════════════════════
void test_write_output_char()
{
    section(L"6. WriteConsoleOutputCharacter — 指定坐标写字符");

    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi{};
    GetConsoleScreenBufferInfo(hOut, &csbi);

    COORD writePos = {0, static_cast<SHORT>(csbi.dwCursorPosition.Y)};
    DWORD written;

    WriteConsoleOutputCharacterW(hOut, L"这是WriteConsoleOutputCharacter 写的", 19, writePos, &written);
    writePos.Y += 1;
    SetConsoleCursorPosition(hOut, writePos);
    wprint(L"\n  上一行应该显示中文 \"这是WriteConsoleOutputCharacter 写的\"\n");

    pause_and_continue(L"检查上一行中文字符是否正确");
}

// ═══════════════════════════════════════════════════════
// 测试 7: WriteConsoleOutputAttribute — 指定位置写属性
// ═══════════════════════════════════════════════════════
void test_write_output_attr()
{
    section(L"7. WriteConsoleOutputAttribute — 指定坐标写属性");

    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi{};
    GetConsoleScreenBufferInfo(hOut, &csbi);

    COORD writePos = csbi.dwCursorPosition;
    // 先写字符
    FillConsoleOutputCharacterW(hOut, L'C', 30, writePos, nullptr);

    // 不同颜色
    WORD attrs[] = {
        FOREGROUND_RED,
        FOREGROUND_GREEN,
        FOREGROUND_BLUE,
        FOREGROUND_RED | FOREGROUND_GREEN,  // 黄
        FOREGROUND_RED | FOREGROUND_BLUE,   // 品
        FOREGROUND_GREEN | FOREGROUND_BLUE, // 青
    };
    for (int i = 0; i < 6; ++i)
    {
        COORD p = {static_cast<SHORT>(writePos.X + i * 5), writePos.Y};
        DWORD n;
        WriteConsoleOutputAttribute(hOut, &attrs[i], 5, p, &n);
    }

    writePos.Y += 2;
    writePos.X = 0;
    SetConsoleCursorPosition(hOut, writePos);
    wprint(L"  上一行: 红绿蓝黄品青, 每组5个 C\n");

    pause_and_continue(L"检查上一行六色是否显示正确");
}

// ═══════════════════════════════════════════════════════
// 测试 8: ScrollConsoleScreenBuffer — 滚动
// ═══════════════════════════════════════════════════════
void test_scroll()
{
    section(L"8. ScrollConsoleScreenBuffer — 滚动屏幕缓冲区");

    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi{};
    GetConsoleScreenBufferInfo(hOut, &csbi);

    // 在当前光标行之后填充一些内容
    SHORT row = csbi.dwCursorPosition.Y;
    for (int i = 0; i < 5; ++i)
    {
        COORD p = {0, static_cast<SHORT>(row + i)};
        wchar_t ch = static_cast<wchar_t>(L'A' + i);
        FillConsoleOutputCharacterW(hOut, ch, 30, p, nullptr);
    }
    csbi.dwCursorPosition.Y = row + 5;
    SetConsoleCursorPosition(hOut, csbi.dwCursorPosition);
    wprint(L"  已写 5 行 (A-E), 等待 1 秒后向上滚动 2 行...\n");
    Sleep(1000);

    SMALL_RECT sr = {0, row, csbi.dwSize.X - 1, static_cast<SHORT>(row + 4)};
    COORD dest = {0, static_cast<SHORT>(row - 2)};
    CHAR_INFO fill{};
    fill.Char.UnicodeChar = L' ';
    fill.Attributes = csbi.wAttributes;
    ScrollConsoleScreenBufferW(hOut, &sr, nullptr, dest, &fill);

    csbi.dwCursorPosition.Y = row + 5;
    SetConsoleCursorPosition(hOut, csbi.dwCursorPosition);
    wprint(L"  已向上滚动 2 行, 观察 A-E 是否整体上移.\n");

    pause_and_continue(L"检查滚动效果");
}

// ═══════════════════════════════════════════════════════
// 测试 9: SetConsoleTextAttribute — 设置文本属性
// ═══════════════════════════════════════════════════════
void test_text_attr()
{
    section(L"9. SetConsoleTextAttribute — 设置文本属性");

    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);

    wprint(L"  默认色: 白色 (灰)\n");

    SetConsoleTextAttribute(hOut, FOREGROUND_RED | FOREGROUND_INTENSITY);
    wprint(L"  亮红色: Hello!\n");

    SetConsoleTextAttribute(hOut, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
    wprint(L"  亮绿色: Hello!\n");

    SetConsoleTextAttribute(hOut, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
    wprint(L"  亮蓝色: Hello!\n");

    SetConsoleTextAttribute(hOut, BACKGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
    wprint(L"  红底亮绿: Hello!\n");

    SetConsoleTextAttribute(hOut, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
    wprint(L"  恢复默认 (灰)\n");

    pause_and_continue(L"检查上面文本颜色是否正确 (亮红/亮绿/亮蓝/红底亮绿/恢复灰)");
}

// ═══════════════════════════════════════════════════════
// 测试 10: Get/SetConsoleCursorInfo — 光标外观
// ═══════════════════════════════════════════════════════
void test_cursor_info()
{
    section(L"10. Get/SetConsoleCursorInfo — 光标外观");

    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_CURSOR_INFO ci{};

    if (GetConsoleCursorInfo(hOut, &ci))
    {
        wprint(L"  当前光标大小: %lu%%, 可见: %s\n", ci.dwSize, ci.bVisible ? L"是" : L"否");
    }

    wprint(L"  设置光标大小为 100%% (大光标), 等待 1.5s...\n");
    ci.dwSize = 100;
    ci.bVisible = TRUE;
    SetConsoleCursorInfo(hOut, &ci);
    Sleep(1500);

    wprint(L"  设置光标大小为 25%% (小光标), 等待 1.5s...\n");
    ci.dwSize = 25;
    SetConsoleCursorInfo(hOut, &ci);
    Sleep(1500);

    wprint(L"  隐藏光标, 等待 1.5s...\n");
    ci.bVisible = FALSE;
    SetConsoleCursorInfo(hOut, &ci);
    Sleep(1500);

    wprint(L"  恢复光标\n");
    ci.bVisible = TRUE;
    ci.dwSize = 25;
    SetConsoleCursorInfo(hOut, &ci);

    pause_and_continue(L"检查光标大小变化 (大→小→隐藏→恢复)");
}

// ═══════════════════════════════════════════════════════
// 测试 11: Get/SetConsoleTitle — 窗口标题
// ═══════════════════════════════════════════════════════
void test_title()
{
    section(L"11. Get/SetConsoleTitle — 窗口标题");

    wchar_t oldTitle[256]{};
    GetConsoleTitleW(oldTitle, 256);
    wprint(L"  旧标题: %s\n", oldTitle);

    SetConsoleTitleW(L"[API Test] ConPTY Title 中文标题 ✓");
    wprint(L"  设置新标题, 查看 WT 标签页...\n");
    Sleep(1500);

    // 恢复
    SetConsoleTitleW(oldTitle);
    wprint(L"  已恢复原标题\n");

    pause_and_continue(L"检查 WT 标签页标题是否变化过");
}

// ═══════════════════════════════════════════════════════
// 测试 12: GetConsoleMode / SetConsoleMode
// ═══════════════════════════════════════════════════════
void test_console_mode()
{
    section(L"12. GetConsoleMode / SetConsoleMode — 控制台模式");

    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode;

    if (GetConsoleMode(hOut, &mode))
    {
        wprint(L"  输出模式: 0x%08lX\n", mode);
        wprint(L"    ENABLE_PROCESSED_OUTPUT:        %s\n", (mode & ENABLE_PROCESSED_OUTPUT) ? L"✓" : L"✗");
        wprint(L"    ENABLE_WRAP_AT_EOL_OUTPUT:      %s\n", (mode & ENABLE_WRAP_AT_EOL_OUTPUT) ? L"✓" : L"✗");
        wprint(L"    ENABLE_VIRTUAL_TERMINAL_PROCESSING: %s\n",
               (mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) ? L"✓" : L"✗");
        wprint(L"    DISABLE_NEWLINE_AUTO_RETURN:    %s\n", (mode & DISABLE_NEWLINE_AUTO_RETURN) ? L"✓" : L"✗");
        wprint(L"    ENABLE_LVB_GRID_WORLDWIDE:      %s\n", (mode & ENABLE_LVB_GRID_WORLDWIDE) ? L"✓" : L"✗");
    }

    if (GetConsoleMode(hIn, &mode))
    {
        wprint(L"  输入模式: 0x%08lX\n", mode);
        wprint(L"    ENABLE_ECHO_INPUT:              %s\n", (mode & ENABLE_ECHO_INPUT) ? L"✓" : L"✗");
        wprint(L"    ENABLE_LINE_INPUT:              %s\n", (mode & ENABLE_LINE_INPUT) ? L"✓" : L"✗");
        wprint(L"    ENABLE_PROCESSED_INPUT:         %s\n", (mode & ENABLE_PROCESSED_INPUT) ? L"✓" : L"✗");
        wprint(L"    ENABLE_VIRTUAL_TERMINAL_INPUT:  %s\n", (mode & ENABLE_VIRTUAL_TERMINAL_INPUT) ? L"✓" : L"✗");
        wprint(L"    ENABLE_WINDOW_INPUT:            %s\n", (mode & ENABLE_WINDOW_INPUT) ? L"✓" : L"✗");
    }

    pause_and_continue(L"检查模式标志是否合理");
}

// ═══════════════════════════════════════════════════════
// 测试 13: GetConsoleCP / GetConsoleOutputCP
// ═══════════════════════════════════════════════════════
void test_codepage()
{
    section(L"13. GetConsoleCP / GetConsoleOutputCP — 代码页");

    wprint(L"  输入代码页 (CP):  %u  (936=GBK, 932=SJIS, 949=Korean, 950=BIG5)\n", GetConsoleCP());
    wprint(L"  输出代码页 (CP):  %u\n", GetConsoleOutputCP());

    if (GetConsoleCP() == 65001)
        wprint(L"  ✓ UTF-8 模式 (推荐)\n");

    pause_and_continue(L"检查代码页");
}

// ═══════════════════════════════════════════════════════
// 测试 14: GetLargestConsoleWindowSize
// ═══════════════════════════════════════════════════════
void test_largest_window_size()
{
    section(L"14. GetLargestConsoleWindowSize — 最大窗口");

    COORD sz = GetLargestConsoleWindowSize(GetStdHandle(STD_OUTPUT_HANDLE));
    wprint(L"  最大窗口: (%d, %d)\n", sz.X, sz.Y);

    pause_and_continue(L"完");
}

// ═══════════════════════════════════════════════════════
// 测试 15: ReadConsole — 交互式输入
// ═══════════════════════════════════════════════════════
void test_read_console()
{
    section(L"15. ReadConsole — 交互式输入测试");

    wprint(L"  请输入一段文字 (以 Enter 结束):\n  >>> ");
    wchar_t buf[256]{};
    DWORD read;
    ReadConsoleW(GetStdHandle(STD_INPUT_HANDLE), buf, 255, &read, nullptr);
    // buf 末尾包含 \r\n
    wprint(L"  ReadConsole 返回 %lu 字符: [", read);
    WriteConsoleW(GetStdHandle(STD_OUTPUT_HANDLE), buf, read, &read, nullptr);
    wprint(L"]\n");

    pause_and_continue(L"检查回显是否和你输入的一致");
}

// ═══════════════════════════════════════════════════════
// main
// ═══════════════════════════════════════════════════════
int main()
{
    // 启用 VT 处理以支持完整的控制台行为
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode;
    GetConsoleMode(hOut, &mode);
    SetConsoleMode(hOut, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);

    wprint(L"\n");
    section(L"ConPTY 控制台 API 手动验证程序");
    wprint(L"  本程序逐一调用 Win32 Console API, 请肉眼观察结果是否正确.\n");
    wprint(L"  每个测试会给出说明文字, 按 Enter 继续下一项.\n");

    test_write_console();
    test_screen_buffer_info();
    test_cursor_position();
    test_fill_character();
    test_fill_attribute();
    test_write_output_char();
    test_write_output_attr();
    test_scroll();
    test_text_attr();
    test_cursor_info();
    test_title();
    test_console_mode();
    test_codepage();
    test_largest_window_size();
    test_read_console();

    section(L"全部 API 测试完成!");
    wprint(L"  如果每个测试看起来都正确, 则 ConPTY API 层工作正常.\n\n");
    return 0;
}
