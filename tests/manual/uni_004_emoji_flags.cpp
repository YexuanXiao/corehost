// uni_004_emoji_flags.cpp — Emoji 国旗 + Keycap + Tag 序列
#include "manual_common.hpp"

int main()
{
    HANDLE hOut = init_console();
    title(L"uni-004",
          L"Emoji — 国旗 + Keycap 序列\n   测试 Regional Indicator 国旗序列 (U+1F1E6+)。\n   期望：14 面国旗各由两个 "
          L"Regional Indicator 组成，\n   终端应将其合并为一面国旗 Emoji。\n   Keycap 序列 (#️⃣) 也应正确渲染。");

    wprint(L"\x1b[1;32m[期望结果]\x1b[0m\n");
    wprint(L"  14 面国旗: 🇨🇳🇺🇸🇯🇵🇰🇷🇩🇪🇫🇷🇬🇧🇷🇺🇧🇷🇮🇳🇦🇺🇨🇦🇮🇹🇪🇸\n");
    wprint(L"  10 个 Keycap: #️⃣*️⃣0️⃣1️⃣2️⃣3️⃣4️⃣5️⃣6️⃣7️⃣8️⃣9️⃣\n");

    sep();
    wprint(L"\x1b[1;32m[实际输出]\x1b[0m\n\n");

    wprint(L"  国旗 (Regional Indicator pairs):\n  ");
    wprint(L"🇨🇳 (CN)  🇺🇸 (US)  🇯🇵 (JP)  🇰🇷 (KR)  🇩🇪 (DE)  🇫🇷 (FR)  🇬🇧 (GB)\n  ");
    wprint(L"🇷🇺 (RU)  🇧🇷 (BR)  🇮🇳 (IN)  🇦🇺 (AU)  🇨🇦 (CA)  🇮🇹 (IT)  🇪🇸 (ES)\n");

    wprint(L"\n  Keycap 序列:\n  ");
    wprint(L"#️⃣ *️⃣ 0️⃣ 1️⃣ 2️⃣ 3️⃣ 4️⃣ 5️⃣ 6️⃣ 7️⃣ 8️⃣ 9️⃣\n");

    wprint(L"\n  \x1b[1;37m验证:\x1b[0m 所有国旗应为整面旗, 非两个字母; Keycap 为按键样式\n");

    wait3s(L"检查国旗和 Keycap 是否正确渲染");
    return 0;
}
