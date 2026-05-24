// uni_003_emoji_basic.cpp — Emoji 基本表情 + 肤色修饰
#include "manual_common.hpp"

int main()
{
    HANDLE hOut = init_console();
    title(L"uni-003", L"Emoji — 基本表情 + 肤色修饰\n   测试 Unicode Emoji (U+1F600+) 的彩色渲染。\n   期望：19 "
                      L"个基本表情、10 个手势、6 种肤色变体。\n   肤色修饰使用 Fitzpatrick Type 修饰符 "
                      L"(U+1F3FB..U+1F3FF)。\n   所有 Emoji 应为彩色, 无黑白豆腐块。");

    wprint(L"\x1b[1;32m[期望结果]\x1b[0m\n");
    wprint(L"  19 个基本表情 (😀😃😄...)\n");
    wprint(L"  10 个手势 (👍👎👌...)\n");
    wprint(L"  6 种肤色: 默认/浅/中浅/中/中深/深\n");
    wprint(L"  ZWJ 序列: 👨‍👩‍👧‍👦 四口家庭\n");

    sep();
    wprint(L"\x1b[1;32m[实际输出]\x1b[0m\n\n");

    wprint(L"  基本表情 (19个):\n  😀 😃 😄 😁 😅 😂 🤣 😊 😇 🙂 🙃 😉 😌 😍 🥰 😘 😗 😙 😚\n");
    wprint(L"  手势 (10个):\n  👍 👎 👌 ✌ 🤞 🤟 🤘 🤙 👋 🤚\n");

    wprint(L"\n  肤色修饰 (👍 + Fitz 1-5):\n");
    wprint(L"  👍 (默认黄)  👍🏻 (浅肤)  👍🏼 (中浅)  👍🏽 (中等)  👍🏾 (中深)  👍🏿 (深肤)\n");

    wprint(L"\n  ZWJ 序列:\n");
    wprint(L"  👨‍👩‍👧‍👦 = 四口家庭 (👨+ZWJ+👩+ZWJ+👧+ZWJ+👦)\n");
    wprint(L"  👩‍💻 = 女程序员 (👩+ZWJ+💻)\n");
    wprint(L"  👨‍🚒 = 男消防员 (👨+ZWJ+🚒)\n");

    wait3s(L"检查 Emoji 彩色渲染和肤色修饰");
    return 0;
}
