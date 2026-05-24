// test_manual_vt.cpp — VT 终端序列手动验证程序
// 用法: 在 ConPTY (corehost.exe) 下运行, 观察 VT 序列是否正常渲染
// 编译: cl /EHsc /std:c++17 /utf-8 test_manual_vt.cpp /Fe:test_vt.exe

#include <windows.h>
#include <cstdio>
#include <cwchar>

// ── 工具: 写宽字符串 ──
void wprint(const wchar_t *fmt, ...)
{
    wchar_t buf[4096];
    va_list args;
    va_start(args, fmt);
    int n = _vsnwprintf_s(buf, _TRUNCATE, fmt, args);
    va_end(args);
    if (n > 0)
    {
        DWORD w;
        WriteConsoleW(GetStdHandle(STD_OUTPUT_HANDLE), buf, static_cast<DWORD>(n), &w, nullptr);
    }
}

// ── 标题分隔 ──
void section(const wchar_t *title)
{
    wprint(L"\n\x1b[1;33m══════════════════════════════════════════════════\x1b[0m\n");
    wprint(L"\x1b[1;33m  %s\x1b[0m\n", title);
    wprint(L"\x1b[1;33m══════════════════════════════════════════════════\x1b[0m\n\n");
}

// ── VT 输出 ──
void vt(const wchar_t *seq)
{
    wprint(L"%s", seq);
}

// ── 按 Enter 继续 ──
void pause_enter(const wchar_t *hint)
{
    wprint(L"\n  >>> %s — 按 Enter 继续...", hint);
    // 吃掉输入缓冲
    FlushConsoleInputBuffer(GetStdHandle(STD_INPUT_HANDLE));
    char buf[16];
    DWORD rd;
    ReadFile(GetStdHandle(STD_INPUT_HANDLE), buf, 1, &rd, nullptr);
    while (rd > 0)
    {
        ReadFile(GetStdHandle(STD_INPUT_HANDLE), buf, 1, &rd, nullptr);
        if (buf[0] == '\n')
            break;
    }
    wprint(L"\n");
}

// ═══════════════════════════════════════════════════════
// 1. 光标定位 CUP / HVP / CHA / VPA
// ═══════════════════════════════════════════════════════
void test_cursor()
{
    section(L"1. 光标定位 (CUP/CHA/VPA/CNL/CPL/CUF/CUB/CUU/CUD)");

    wprint(L"  光标移到 (5,1) — ESC[1;5H\n");
    vt(L"\x1b[1;5H");
    wprint(L"(位置 5,1)");
    Sleep(500);

    wprint(L"\n  移到 (20,3) — ESC[3;20H\n");
    vt(L"\x1b[3;20H");
    wprint(L"(位置 20,3)");
    Sleep(500);

    wprint(L"\n  CHA 移到第 40 列 — ESC[40G\n");
    vt(L"\x1b[40G");
    wprint(L"(列 40, 行不变)");
    Sleep(500);

    wprint(L"\n  VPA 移到第 5 行 — ESC[5d\n");
    vt(L"\x1b[5d");
    wprint(L"(行 5, 列不变)");
    Sleep(500);

    wprint(L"\n  CNL 下移 2 行到行首 — ESC[2E → ");
    vt(L"\x1b[2E");
    wprint(L"(看光标是否在行首)");
    Sleep(500);

    wprint(L"\n  CPL 上移 1 行到行首 — ESC[1F → ");
    vt(L"\x1b[1F");
    wprint(L"(看光标是否在行首)");
    Sleep(500);

    wprint(L"\n  CUF 右移 10 列 — ESC[10C → (光标右移了10)");
    vt(L"\x1b[10C");

    wprint(L"\n  CUB 左移 5 列 — ESC[5D → (光标左移了5)");
    vt(L"\x1b[5D");

    wprint(L"\n  CUU 上移 2 行 — ESC[2A → (光标上移)");
    vt(L"\x1b[2A");

    wprint(L"\n  CUD 下移 3 行 — ESC[3B → (光标下移)");
    vt(L"\x1b[3B");

    pause_enter(L"检查光标移动是否正确");
}

// ═══════════════════════════════════════════════════════
// 2. SGR — 16 色前景/背景
// ═══════════════════════════════════════════════════════
void test_sgr_16color()
{
    section(L"2. SGR — 16 色文本 (前景+背景)");
    wprint(L"  前 8 色 (30-37): ");
    for (int i = 0; i < 8; ++i)
        wprint(L"\x1b[%dm  ■ Col%d \x1b[0m", 30 + i, i);
    wprint(L"\n  亮 8 色 (90-97): ");
    for (int i = 0; i < 8; ++i)
        wprint(L"\x1b[%dm  ■ Col%d \x1b[0m", 90 + i, i);
    wprint(L"\n  背景 8 色 (40-47): ");
    for (int i = 0; i < 8; ++i)
        wprint(L"\x1b[%dm  Bg%d  \x1b[0m", 40 + i, i);
    pause_enter(L"检查 16 色前景+背景是否正确");
}

// ═══════════════════════════════════════════════════════
// 3. SGR — 256 色 + True Color
// ═══════════════════════════════════════════════════════
void test_sgr_extended()
{
    section(L"3. SGR — 256 色 + True Color (RGB)");
    wprint(L"  256 色样本 (索引 0-15):\n  ");
    for (int i = 0; i < 16; ++i)
        wprint(L"\x1b[38;5;%dm  ■  \x1b[0m", i);
    wprint(L"\n  256 色样本 (索引 196-231 是连续色块):\n  ");
    for (int i = 196; i <= 231; ++i)
        wprint(L"\x1b[48;5;%dm  \x1b[0m", i);
    wprint(L"\n  True Color RGB 文本:\n  ");
    wprint(L"\x1b[38;2;255;128;64m  ■ RGB(255,128,64) 暖橙色 \x1b[0m\n");
    wprint(L"  \x1b[38;2;64;128;255m  ■ RGB(64,128,255) 浅蓝色 \x1b[0m\n");
    wprint(L"  True Color RGB 背景:\n  ");
    wprint(L"\x1b[48;2;200;50;50m                                     \x1b[0m\n");
    wprint(L"  \x1b[48;2;255;200;0m                                     \x1b[0m\n");
    wprint(L"  \x1b[48;2;0;150;255m                                     \x1b[0m");
    pause_enter(L"检查 256 色和 True Color 是否正确");
}

// ═══════════════════════════════════════════════════════
// 4. SGR — 属性 (粗体/斜体/下划线/删除线/反显...)
// ═══════════════════════════════════════════════════════
void test_sgr_attrs()
{
    section(L"4. SGR — 文本属性");

    wprint(L"  \x1b[1m 粗体 Bold       \x1b[0m\n");
    wprint(L"  \x1b[3m 斜体 Italic     \x1b[0m\n");
    wprint(L"  \x1b[4m 下划线 Underline \x1b[0m\n");
    wprint(L"  \x1b[9m 删除线 Strikethrough \x1b[0m\n");
    wprint(L"  \x1b[2m 弱化 Faint/Dim  \x1b[0m\n");
    wprint(L"  \x1b[5m 慢闪 Blink      \x1b[0m (可能不支持)\n");
    wprint(L"  \x1b[7m 反显 Reverse     \x1b[0m\n");
    wprint(L"  \x1b[53m 上划线 Overline  \x1b[0m\n");
    wprint(L"  组合: \x1b[1;3;4;9m 粗+斜+下划线+删除线 \x1b[0m\n");

    pause_enter(L"检查文本属性是否正确渲染");
}

// ═══════════════════════════════════════════════════════
// 5. ED — 擦除显示
// ═══════════════════════════════════════════════════════
void test_erase_display()
{
    section(L"5. ED — 擦除显示 (ESC[PsJ)");
    wprint(L"  正在执行 ED2 (清除全屏) — ESC[2J...\n");
    Sleep(1000);
    vt(L"\x1b[2J");
    wprint(L"  全屏已清除。\n");
    wprint(L"  ED0 (从光标擦到屏尾): 光标在 (20,Y), 擦除本行及以下...\n");
    // 先写一些文字
    for (int i = 0; i < 3; ++i)
        wprint(L"  这一行将被擦除 ───────────────────────\n");
    vt(L"\x1b[s");   // 保存光标
    vt(L"\x1b[20G"); // 移到第20列
    vt(L"\x1b[0J");  // ED0
    vt(L"\x1b[u");   // 恢复光标
    wprint(L"  (ED0 执行完毕, 光标右侧及以下应被擦除)\n");
    pause_enter(L"检查 ED0 擦除效果");
}

// ═══════════════════════════════════════════════════════
// 6. EL — 擦除行
// ═══════════════════════════════════════════════════════
void test_erase_line()
{
    section(L"6. EL — 擦除行 (ESC[PsK)");
    wprint(L"  EL0 (光标到行尾): 本行\x1b[20G←光标→后面应被擦除\n");
    vt(L"\x1b[20G");
    vt(L"\x1b[0K");
    Sleep(500);
    wprint(L"\n  EL1 (行首到光标): 前面应被擦除 → 只剩下"
           "\x1b[1K"
           L"这段");
    Sleep(500);
    wprint(L"\n  EL2 (整行): ");
    wprint(L"这整行会被擦除\x1b[2K(擦除后这里显示)");
    pause_enter(L"检查 EL 效果");
}

// ═══════════════════════════════════════════════════════
// 7. DECSC / DECRC / DECSTBM (保存恢复光标 + 滚动区域)
// ═══════════════════════════════════════════════════════
void test_save_restore()
{
    section(L"7. DECSC/DECRC 光标保存恢复 + 滚动区域");

    wprint(L"  ESC7 保存光标 → 移到 (30,10) → 等 1s → ESC8 恢复:");
    vt(L"\x1b7"); // DECSC
    vt(L"\x1b[10;30H");
    wprint(L"(30,10)");
    Sleep(1000);
    vt(L"\x1b8"); // DECRC
    wprint(L"  ← 光标恢复到了原位\n");

    wprint(L"  DECSTBM 滚动区域 (5-15), 然后写 20 行:\n");
    vt(L"\x1b[5;15r"); // 设置滚动区域
    for (int i = 0; i < 20; ++i)
        wprint(L"  第 %d 行, 仅 5-15 区域滚动\n", i + 1);
    vt(L"\x1b[r"); // 重置滚动区域
    wprint(L"  滚动区域已恢复\n");

    pause_enter(L"检查滚动区域是否仅 5-15 行滚动");
}

// ═══════════════════════════════════════════════════════
// 8. 光标显示/隐藏/形状
// ═══════════════════════════════════════════════════════
void test_cursor_style()
{
    section(L"8. 光标显示/隐藏/形状");

    wprint(L"  隐藏光标 ESC[?25l → ");
    vt(L"\x1b[?25l");
    Sleep(1000);
    wprint(L"(光标隐藏) → 显示 ESC[?25h → ");
    vt(L"\x1b[?25h");
    wprint(L"(光标恢复)\n");

    wprint(L"  光标形状:\n");
    wprint(L"    DECSCUSR 0 (默认): ");
    vt(L"\x1b[0 q");
    Sleep(800);
    wprint(L"\n    DECSCUSR 1 (闪烁块): ");
    vt(L"\x1b[1 q");
    Sleep(800);
    wprint(L"\n    DECSCUSR 2 (稳态块): ");
    vt(L"\x1b[2 q");
    Sleep(800);
    wprint(L"\n    DECSCUSR 3 (闪烁下划线): ");
    vt(L"\x1b[3 q");
    Sleep(800);
    wprint(L"\n    DECSCUSR 4 (稳态下划线): ");
    vt(L"\x1b[4 q");
    Sleep(800);
    wprint(L"\n    DECSCUSR 5 (闪烁竖线): ");
    vt(L"\x1b[5 q");
    Sleep(800);
    wprint(L"\n    DECSCUSR 6 (稳态竖线): ");
    vt(L"\x1b[6 q");
    Sleep(800);
    vt(L"\x1b[0 q");
    wprint(L"\n    恢复默认\n");

    pause_enter(L"检查光标形状变化 (块/下划线/竖线)");
}

// ═══════════════════════════════════════════════════════
// 9. SU/SD — 滚动 + IL/DL — 插入/删除行
// ═══════════════════════════════════════════════════════
void test_scroll_insert_delete()
{
    section(L"9. SU/SD 滚动 + IL/DL 插入/删除行");
    wprint(L"  SU 向上滚动 2 行 → ");
    vt(L"\x1b[2S");
    wprint(L"(内容向下滚了 2 行)\n");
    wprint(L"  SD 向下滚动 2 行 → ");
    vt(L"\x1b[2T");
    wprint(L"(内容向上滚了 2 行)\n");

    wprint(L"  IL 插入 2 行 (当前光标行下移) → ");
    vt(L"\x1b[2L");
    wprint(L"(应该插入了空白行)\n");
    wprint(L"  DL 删除 2 行 (移除当前光标行) → ");
    vt(L"\x1b[2M");
    wprint(L"(删除了上面插入的行)\n");

    pause_enter(L"检查滚动和插入/删除行");
}

// ═══════════════════════════════════════════════════════
// 10. OSC — 窗口标题 / 通知 / 超链接
// ═══════════════════════════════════════════════════════
void test_osc()
{
    section(L"10. OSC — 窗口标题 / 通知");

    wchar_t oldTitle[256]{};
    GetConsoleTitleW(oldTitle, 256);

    wprint(L"  OSC 2 设置标题为 'VT Test 标题':\n");
    vt(L"\x1b]2;VT Test 标题\x1b\\");
    Sleep(1000);

    wprint(L"  OSC 0 设置图标名+标题为 'Icon VT':\n");
    vt(L"\x1b]0;Icon VT\x1b\\");
    Sleep(1000);

    // 恢复
    wprint(L"\x1b]2;%s\x1b\\", oldTitle);
    wprint(L"  已恢复原标题\n");

    pause_enter(L"检查标题是否改变");
}

// ═══════════════════════════════════════════════════════
// 11. REP / DCH / ICH — 重复和字符操作
// ═══════════════════════════════════════════════════════
void test_repeat_and_char()
{
    section(L"11. REP — 重复字符 / DCH / ICH");

    wprint(L"  REP: 重复前一个字符 10 次 → A");
    vt(L"\x1b[10b");
    wprint(L"  ← 应该有 10 个 'A'\n");

    wprint(L"  ICH 插入 5 空白字符 → 后面文字右移:\n  ABCDE");
    vt(L"\x1b[3G\x1b[5@");
    wprint(L"  ← FGHI 被右移了\n");

    wprint(L"  DCH 删除 5 字符 → 后面文字左移:\n  ");
    vt(L"\x1b[5P");
    wprint(L"← 被右移的内容恢复\n");

    pause_enter(L"检查 REP/ICH/DCH 效果");
}

// ═══════════════════════════════════════════════════════
// 12. 设备报告 + 设备属性 (DA/DSR)
// ═══════════════════════════════════════════════════════
void test_device_reports()
{
    section(L"12. 设备报告 (DSR/DA)");

    wprint(L"  DSR 6 — 请求光标位置报告 ESC[6n:\n");
    wprint(L"  终端应在输入流中回复 ESC[Pn;PnR, ConPTY 内部处理.\n");
    wprint(L"  VT 级别检查: ");

    wprint(L"\n  DA — 请求设备属性 ESC[c:\n");
    wprint(L"  (如果终端支持, 会在输出中看到回应)\n");

    pause_enter(L"此处仅说明, 无法肉眼验证输入流");
}

// ═══════════════════════════════════════════════════════
// 13. 自动换行 / 原点模式 / 132列
// ═══════════════════════════════════════════════════════
void test_dec_modes()
{
    section(L"13. DEC 私有模式");

    wprint(L"  DECAWM 自动换行 (默认开): 写 80 个 X 到行尾 →\n");
    for (int i = 0; i < 80; ++i)
        wprint(L"X");
    wprint(L"  ← 自动换到了下一行\n");

    wprint(L"  DECAWM 关闭 (ESC[?7l):\n");
    vt(L"\x1b[?7l");
    for (int i = 0; i < 80; ++i)
        wprint(L"Y");
    wprint(L"  ← 最后一字符覆盖在行尾, 没有换行\n");
    vt(L"\x1b[?7h");
    wprint(L"  恢复自动换行\n");

    pause_enter(L"检查自动换行开关效果");
}

// ═══════════════════════════════════════════════════════
// 14. 选择性擦除 + 字符集
// ═══════════════════════════════════════════════════════
void test_charset()
{
    section(L"14. DEC 特殊字符集 (线条绘制)");

    wprint(L"  ESC(0 切换到 DEC 特殊图形字符集:\n  ");
    vt(L"\x1b(0");
    wprint(L"jklmnqtuvwx"); // 线条字符
    vt(L"\x1b(B");
    wprint(L"\n  ESC(B 恢复 ASCII 字符集\n");
    wprint(L"  上面应该显示: ┘┐┌└┼─├┤┴┬ (或类似的线条字符)\n");

    pause_enter(L"检查线条字符是否正确");
}

// ═══════════════════════════════════════════════════════
// 15. 交替缓冲区
// ═══════════════════════════════════════════════════════
void test_alt_buffer()
{
    section(L"15. 交替缓冲区 (Alternate Screen)");

    wprint(L"  切换到交替缓冲区 ESC[?1049h...\n");
    vt(L"\x1b[?1049h");
    wprint(L"  (屏幕清空, 进入交替缓冲区)\n");
    wprint(L"  这是交替缓冲区中的内容.\n");
    wprint(L"  3 秒后切回主缓冲区 ESC[?1049l...\n");
    Sleep(3000);
    vt(L"\x1b[?1049l");
    wprint(L"  回到主缓冲区, 之前的内容应恢复.\n");

    pause_enter(L"检查交替缓冲区切换");
}

// ═══════════════════════════════════════════════════════
// 16. RIS 硬复位
// ═══════════════════════════════════════════════════════
void test_ris()
{
    section(L"16. RIS — 硬复位 (终端重置)");

    wprint(L"  即将发送 ESCc 硬复位终端...\n");
    wprint(L"  注意: 这可能断开 ConPTY 连接!\n");
    wprint(L"  如果确认, 按 Enter; 否则 Ctrl+C 退出.\n");
    FlushConsoleInputBuffer(GetStdHandle(STD_INPUT_HANDLE));
    char buf[16];
    DWORD rd;
    ReadFile(GetStdHandle(STD_INPUT_HANDLE), buf, 1, &rd, nullptr);

    wprint(L"  发送 RIS...\n");
    vt(L"\x1bc");
    wprint(L"  如果还看到这行, RIS 未触发或已在 ConPTY 层被拦截.\n");
    pause_enter(L"检查 RIS 效果");
}

// ═══════════════════════════════════════════════════════
// 17. 窗口大小调整
// ═══════════════════════════════════════════════════════
void test_resize()
{
    section(L"17. 窗口大小调整 (ESC[8;h;w t)");

    wprint(L"  发送 ESC[8;30;100t 请求调整为 30行×100列...\n");
    vt(L"\x1b[8;30;100t");
    wprint(L"  观察 WT 窗口是否改变大小.\n");
    Sleep(2000);
    wprint(L"  恢复: ESC[8;25;80t\n");
    vt(L"\x1b[8;25;80t");

    pause_enter(L"检查窗口大小调整");
}

// ═══════════════════════════════════════════════════════
// 18. 综合: 彩色框 + 对齐 + 进度条效果
// ═══════════════════════════════════════════════════════
void test_comprehensive()
{
    section(L"18. 综合演示: 彩色框 + 进度条");

    wprint(L"  ┌─────────────────────────────────────────┐\n");
    for (int r = 0; r < 5; ++r)
    {
        wprint(L"  │");
        for (int c = 0; c < 41; ++c)
        {
            int hue = (c * 6 + r * 30) % 216 + 16;
            wprint(L"\x1b[48;5;%dm \x1b[0m", hue);
        }
        wprint(L"│\n");
    }
    wprint(L"  └─────────────────────────────────────────┘\n");

    wprint(L"\n  进度条效果:\n");
    for (int p = 0; p <= 100; p += 10)
    {
        int bar = p / 2;
        wprint(L"  [\x1b[42m");
        for (int i = 0; i < bar; ++i)
            vt(L" ");
        vt(L"\x1b[0m");
        for (int i = bar; i < 50; ++i)
            vt(L" ");
        wprint(L"] %3d%%\n", p);
        Sleep(150);
    }

    pause_enter(L"检查彩色框和进度条效果");
}

// ═══════════════════════════════════════════════════════
// main
// ═══════════════════════════════════════════════════════
int main()
{
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode;
    GetConsoleMode(hOut, &mode);
    SetConsoleMode(hOut, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING | DISABLE_NEWLINE_AUTO_RETURN);
    // 也启用 VT 输入
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    GetConsoleMode(hIn, &mode);
    SetConsoleMode(hIn, mode | ENABLE_VIRTUAL_TERMINAL_INPUT);

    vt(L"\x1b[2J\x1b[H"); // 清屏
    section(L"ConPTY VT 终端序列手动验证程序");
    wprint(L"  本程序发送各种 VT/ANSI 转义序列, 请肉眼观察终端渲染.\n");
    wprint(L"  每个测试给出说明 + 视觉效果, 按 Enter 进入下一项.\n");

    test_cursor();
    test_sgr_16color();
    test_sgr_extended();
    test_sgr_attrs();
    test_erase_display();
    test_erase_line();
    test_save_restore();
    test_cursor_style();
    test_scroll_insert_delete();
    test_osc();
    test_repeat_and_char();
    test_device_reports();
    test_dec_modes();
    test_charset();
    test_alt_buffer();
    test_resize();
    test_comprehensive();
    test_ris(); // 放最后

    section(L"全部 VT 测试完成!");
    wprint(L"  希望一切渲染正常!\n\n");
    return 0;
}
