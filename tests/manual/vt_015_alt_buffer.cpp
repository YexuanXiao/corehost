// vt_015_alt_buffer.cpp — 交替缓冲区
#include "manual_common.hpp"

int main()
{
    HANDLE hOut = init_console();
    title(L"vt-015", L"交替缓冲区 (Alternate Screen)\n   测试 ESC[?1049h (切换到交替缓冲区) 和 ESC[?1049l (切回)。\n   "
                     L"期望：切换到交替缓冲区时屏幕清空。\n   在交替缓冲区中写内容。3秒后切回主缓冲区，\n   "
                     L"之前的主缓冲区内容应恢复。");

    wprint(L"\x1b[1;32m[期望结果]\x1b[0m\n");
    wprint(L"  阶段1: 主缓冲区 (当前内容)\n");
    wprint(L"  阶段2: ESC[?1049h → 交替缓冲区 (清屏, 写新内容)\n");
    wprint(L"  阶段3: ESC[?1049l → 回到主缓冲区 (原内容恢复)\n");

    sep();
    wprint(L"\x1b[1;32m[实际输出]\x1b[0m\n\n");

    wprint(L"  [主缓冲区] 这是主缓冲区的内容，稍后应恢复。\n");
    wprint(L"  切换到交替缓冲区...\n");
    Sleep(1000);
    vt(L"\x1b[?1049h");
    wprint(L"  [交替缓冲区] 屏幕已清空。这是交替缓冲区的新内容。\n");
    wprint(L"              3 秒后切回主缓冲区...\n");
    Sleep(3000);
    vt(L"\x1b[?1049l");
    wprint(L"  [主缓冲区] 已切回。上方主缓冲区内容应已恢复。\n");

    wait3s(L"检查交替缓冲区切换前后内容是否恢复");
    return 0;
}
