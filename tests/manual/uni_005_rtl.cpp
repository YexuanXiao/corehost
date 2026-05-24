// uni_005_rtl.cpp — RTL 阿拉伯语 + 希伯来语 + 双向文
#include "manual_common.hpp"

int main()
{
    HANDLE hOut = init_console();
    title(L"uni-005", L"RTL 文本 — 阿拉伯语 / 希伯来语 / 双向文\n   测试从右到左 (RTL) 文字渲染。\n   "
                      L"期望：阿拉伯语和希伯来语应从右向左排列。\n   阿拉伯字母应为连写 (cursive) 形式。\n   双向文 "
                      L"(Bidi) 中英文从左到右、阿文从右到左共存。");

    wprint(L"\x1b[1;32m[期望结果]\x1b[0m\n");
    wprint(L"  阿拉伯语: 从右向左, 字母连写\n");
    wprint(L"  希伯来语: 从右向左\n");
    wprint(L"  双向文: 英文 LTR + 阿文 RTL 混合正确\n");

    sep();
    wprint(L"\x1b[1;32m[实际输出]\x1b[0m\n\n");

    wprint(L"  ── 阿拉伯语 (RTL) ──\n");
    wprint(L"  你好世界:  مرحبا بالعالم\n");
    wprint(L"  长句:      اللغة العربية هي لغة جميلة.\n");

    wprint(L"\n  ── 希伯来语 (RTL) ──\n");
    wprint(L"  你好世界:  שלום עולם!\n");
    wprint(L"  描述:      .עברית היא שפה יפה\n");

    wprint(L"\n  ── 双向文 (Bidi) ──\n");
    wprint(L"  混合: Hello مرحبا World\n");
    wprint(L"  三语: English - עברית - 中文\n");

    wprint(L"\n  \x1b[1;37m验证:\x1b[0m 阿拉伯/希伯来文应从右向左, 字母连写\n");

    wait3s(L"检查 RTL 方向 + 阿拉伯连写");
    return 0;
}
