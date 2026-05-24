// uni_017_mixed_emoji_text.cpp — Emoji + 文本混合
#include "manual_common.hpp"

int main()
{
    HANDLE hOut = init_console();
    title(L"uni-017", L"Emoji + 文本混合渲染\n   测试 Emoji 与中英文文本混合时的对齐和颜色。\n   期望：Emoji "
                      L"在文本行内正确居中，不影响行高和对齐。\n   彩色 Emoji 与单色文本共存时各自颜色正确。\n   Emoji "
                      L"宽度 (通常 2 列) 与中文字符 (2 列) 对齐一致。");

    wprint(L"\x1b[1;32m[期望结果]\x1b[0m\n");
    wprint(L"  混合行内 Emoji 和文字, Emoji 宽度=2列\n");
    wprint(L"  Emoji + 中文 均占 2 列宽度, 可对齐\n");

    sep();
    wprint(L"\x1b[1;32m[实际输出]\x1b[0m\n\n");

    wprint(L"  天气场景:\n  ");
    wprint(L"今天天气真好 ☀️, 温度 25°C, 适合 🏃‍♂️ 跑步!\n");

    wprint(L"\n  食物场景:\n  ");
    wprint(L"🍕 + 🍔 = 😋, 但 🥗 更健康.\n");

    wprint(L"\n  工作场景:\n  ");
    wprint(L"编程 💻 + 咖啡 ☕ = 🚀 生产力\n");

    wprint(L"\n  状态标记 + 中文:\n  ");
    wprint(L"✅ 任务完成  ❌ 测试失败  ⚠️ 警告  ℹ️ 信息\n");

    wprint(L"\n  日文 + Emoji:\n  ");
    wprint(L"🗾 日本 🇯🇵  🗼 東京 🗾\n");

    wprint(L"\n  \x1b[1;37m验证:\x1b[0m Emoji 彩色, 文字正常, 对齐一致\n");

    wait3s(L"检查 Emoji 与文字混合的对齐和颜色");
    return 0;
}
