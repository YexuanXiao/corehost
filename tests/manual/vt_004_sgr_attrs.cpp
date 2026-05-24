// vt_004_sgr_attrs.cpp — SGR 文本属性
#include "manual_common.hpp"

int main()
{
    HANDLE hOut = init_console();
    title(L"vt-004", L"SGR — 文本属性 (粗体/斜体/下划线/删除线/反显等)\n   测试 ESC[1m (粗体), ESC[3m (斜体), ESC[4m "
                     L"(下划线),\n   ESC[9m (删除线), ESC[2m (弱化), ESC[5m (慢闪),\n   ESC[7m (反显), ESC[53m "
                     L"(上划线), 及组合属性。\n   期望：每行文本有明显对应的视觉效果。");

    wprint(L"\x1b[1;32m[期望结果]\x1b[0m\n");
    wprint(L"  行1: 粗体 Bold\n");
    wprint(L"  行2: 斜体 Italic\n");
    wprint(L"  行3: 下划线 Underline\n");
    wprint(L"  行4: 删除线 Strikethrough\n");
    wprint(L"  行5: 弱化 Faint/Dim (颜色较淡)\n");
    wprint(L"  行6: 反显 Reverse (前景背景交换)\n");
    wprint(L"  行8: 粗+斜+下划线+删除线 组合\n");

    sep();
    wprint(L"\x1b[1;32m[实际输出]\x1b[0m\n\n");

    wprint(L"  \x1b[1m粗体 Bold       — 应明显加粗\x1b[0m\n");
    wprint(L"  \x1b[3m斜体 Italic     — 应倾斜\x1b[0m\n");
    wprint(L"  \x1b[4m下划线 Underline — 应有下划线\x1b[0m\n");
    wprint(L"  \x1b[9m删除线 Strikethrough — 应有中划线\x1b[0m\n");
    wprint(L"  \x1b[2m弱化 Faint/Dim  — 应颜色较淡\x1b[0m\n");
    wprint(L"  \x1b[5m慢闪 Blink      — 可能闪烁或不支持\x1b[0m\n");
    wprint(L"  \x1b[7m反显 Reverse     — 应前背景色交换\x1b[0m\n");
    wprint(L"  \x1b[53m上划线 Overline  — 应有上划线\x1b[0m\n");
    wprint(L"  \x1b[1;3;4;9m粗+斜+下划线+删除线 组合\x1b[0m\n");

    wait3s(L"检查各文本属性视觉效果");
    return 0;
}
