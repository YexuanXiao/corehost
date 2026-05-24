// test_manual_unicode.cpp — Unicode 功能手动验证程序
// 测试: 组合字符、Emoji、RTL、CJK、全角等
// 编译: cl /EHsc /std:c++17 /utf-8 test_manual_unicode.cpp /Fe:test_unicode.exe

#include <windows.h>
#include <cstdio>
#include <cwchar>

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

void vt(const wchar_t *seq)
{
    wprint(L"%s", seq);
}

void section(const wchar_t *title)
{
    wprint(L"\n\x1b[1;36m══════════════════════════════════════════════════\x1b[0m\n");
    wprint(L"\x1b[1;36m  %s\x1b[0m\n", title);
    wprint(L"\x1b[1;36m══════════════════════════════════════════════════\x1b[0m\n\n");
}

void pause_enter(const wchar_t *hint)
{
    wprint(L"\n  >>> %s — 按 Enter 继续...", hint);
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
// 1. 基本多语言面 (BMP) — 各语言区块
// ═══════════════════════════════════════════════════════
void test_bmp_languages()
{
    section(L"1. 基本多语言面 (BMP) — 各语言文字");

    wprint(L"  ASCII:        Hello, World! 12345\n");
    wprint(L"  拉丁扩展:     é à ö ñ ç ß Å Ø\n");
    wprint(L"  希腊字母:     ΑΒΓΔΕ αβγδε\n");
    wprint(L"  西里尔字母:   Привет, мир! (俄语'你好世界')\n");
    wprint(L"  中文 (CJK):    你好世界！测试文字显示\n");
    wprint(L"  日文:         こんにちは世界！\n");
    wprint(L"  韩文:         안녕하세요 세계! (조합형)\n");
    wprint(L"  泰文:         สวัสดีชาวโลก\n");
    wprint(L"  阿拉伯文:     مرحبا بالعالم\n");
    wprint(L"  希伯来文:     שלום עולם\n");
    wprint(L"  天城文:       नमस्ते दुनिया\n");
    wprint(L"  越南文:       Xin chào thế giới (tóan bộ)\n");

    pause_enter(L"检查各语言文字是否正常显示 (无乱码/豆腐块)");
}

// ═══════════════════════════════════════════════════════
// 2. 组合字符 (Combining Characters)
// ═══════════════════════════════════════════════════════
void test_combining()
{
    section(L"2. 组合字符 — 基础字母 + 附加符号");

    wprint(L"  ── Latin 组合 ──\n");
    wprint(L"  e + ◌́  = e\u0301  (应显示 é)\n");
    wprint(L"  a + ◌̂  = a\u0302  (应显示 â)\n");
    wprint(L"  o + ◌̈  = o\u0308  (应显示 ö)\n");
    wprint(L"  n + ◌̃  = n\u0303  (应显示 ñ)\n");
    wprint(L"  c + ◌̧  = c\u0327  (应显示 ç)\n");
    wprint(L"  u + ◌̆  = u\u0306  (应显示 ŭ)\n");

    wprint(L"\n  ── 多重组合 ──\n");
    wprint(L"  a + ◌̂ + ◌̃ = a\u0302\u0303 (ấ, 越南语)\n");
    wprint(L"  A + ◌̊ = A\u030A (Å 的分解形式)\n");
    wprint(L"  e + ◌̄ + ◌́ + ◌̖ = e\u0304\u0301\u0316 (多重组合)\n");

    wprint(L"\n  ── 希腊组合 ──\n");
    wprint(L"  α + ◌̓ + ◌́ = α\u0313\u0301 (ἄ, 希腊语 Spiritus + Acute)\n");

    wprint(L"\n  ── 西里尔组合 ──\n");
    wprint(L"  и + ◌̆ = и\u0306 (й 的分解, 俄语)\n");

    pause_enter(L"检查组合字符是否正确叠放 (符号应在字母上方/下方)");
}

// ═══════════════════════════════════════════════════════
// 3. Emoji — 基本表情 + 修饰符
// ═══════════════════════════════════════════════════════
void test_emoji_basic()
{
    section(L"3. Emoji — 基本表情 + 肤色修饰");

    wprint(L"  基本表情:\n");
    wprint(L"  😀 😃 😄 😁 😅 😂 🤣 😊 😇 🙂 🙃 😉 😌 😍 🥰 😘 😗 😙 😚\n");
    wprint(L"  😋 😛 😝 😜 🤪 🤨 🧐 🤓 😎 🤩 🥳 😏 😒 😞 😔 😟 😕 🙁\n");

    wprint(L"\n  手势:\n");
    wprint(L"  👍 👎 👌 ✌ 🤞 🤟 🤘 🤙 👋 🤚 🖐 ✋ 🖖 👌 🤏 ✌ 🤞\n");

    wprint(L"\n  肤色修饰 (Fitzpatrick Type 1-5):\n");
    wprint(L"  👍 = 默认黄; 👍🏻 = 浅肤色; 👍🏼 = 中浅; 👍🏽 = 中等; 👍🏾 = 中深; 👍🏿 = 深肤\n");
    wprint(L"  注意: 肤色修饰符是 U+1F3FB..U+1F3FF (👍U+1F44D + 🏻U+1F3FB)\n");

    wprint(L"\n  家庭/职业 (ZWJ 序列):\n");
    wprint(L"  👨‍👩‍👧‍👦 = 四口家庭 (👨+ZWJ+👩+ZWJ+👧+ZWJ+👦)\n");
    wprint(L"  👩‍💻 = 女程序员 (👩+ZWJ+💻)\n");
    wprint(L"  👨‍🚒 = 男消防员\n");

    pause_enter(L"检查 Emoji 是否正确渲染 (彩色/无豆腐块)");
}

// ═══════════════════════════════════════════════════════
// 4. Emoji — 国旗 + 其他序列
// ═══════════════════════════════════════════════════════
void test_emoji_flags()
{
    section(L"4. Emoji — 国旗 + 其他序列");

    wprint(L"  国旗 (Regional Indicator pairs):\n");
    wprint(L"  🇨🇳 (CN)  🇺🇸 (US)  🇯🇵 (JP)  🇰🇷 (KR)  🇩🇪 (DE)  🇫🇷 (FR)  🇬🇧 (GB)\n");
    wprint(L"  🇷🇺 (RU)  🇧🇷 (BR)  🇮🇳 (IN)  🇦🇺 (AU)  🇨🇦 (CA)  🇮🇹 (IT)  🇪🇸 (ES)\n");

    wprint(L"\n  Keycap 序列:\n");
    wprint(L"  #️⃣ *️⃣ 0️⃣ 1️⃣ 2️⃣ 3️⃣ 4️⃣ 5️⃣ 6️⃣ 7️⃣ 8️⃣ 9️⃣\n");

    wprint(L"\n  Tag 序列:\n");
    wprint(L"  🏴󠁧󠁢󠁥󠁮󠁧󠁿 = 英格兰旗 (tag sequence)\n");

    pause_enter(L"检查国旗是否正确渲染 (两个字母拼成的国旗)");
}

// ═══════════════════════════════════════════════════════
// 5. RTL 文本 — 阿拉伯语 + 希伯来语 + 双向文
// ═══════════════════════════════════════════════════════
void test_rtl()
{
    section(L"5. RTL 文本 — 阿拉伯语 / 希伯来语 / 双向文");

    wprint(L"  ── 阿拉伯语 (RTL) ──\n");
    wprint(L"  独立:  مرحبا بالعالم  (你好世界)\n");
    wprint(L"  长句:  اللغة العربية هي إحدى أكثر اللغات انتشارًا في العالم.\n");
    wprint(L"  注意: 阿拉伯字母应当是连写的 (cursive), 从右向左阅读.\n");

    wprint(L"\n  ── 希伯来语 (RTL) ──\n");
    wprint(L"  שלום עולם! (你好世界!)\n");
    wprint(L"  .עברית היא שפה יפה (希伯来语是美丽的语言)\n");

    wprint(L"\n  ── 双向文混合 (Bidi) ──\n");
    wprint(L"  Hello مرحبا World → 英文从左到右, 阿拉伯文从右到左\n");
    wprint(L"  English - עברית - 中文 → 三种方向混合\n");

    wprint(L"\n  ── RTL 控制符 ──\n");
    wprint(L"  RLM (U+200F) 和 LRM (U+200E) 用于强制方向:\n");
    wprint(L"  数字 12345 \u200Fשלום  ← 数字在左, 希伯来文在右\n");

    pause_enter(L"检查 RTL 文本是否从右向左, 阿拉伯字母是否连写");
}

// ═══════════════════════════════════════════════════════
// 6. CJK — 全角/半角 + 扩展汉字
// ═══════════════════════════════════════════════════════
void test_cjk()
{
    section(L"6. CJK — 全角/半角 + 扩展汉字");

    wprint(L"  ── 全角 ASCII ──\n");
    wprint(L"  半角: ABCabc123\n");
    wprint(L"  全角: ＡＢＣａｂｃ１２３ (FF21-FF3A)\n");

    wprint(L"\n  ── 全角片假名 ──\n");
    wprint(L"  半角: ｶﾀｶﾅ (FF66-FF9F)\n");
    wprint(L"  全角: カタカナ\n");

    wprint(L"\n  ── CJK 扩展区汉字 ──\n");
    wprint(L"  BMP 常用:  你好世界汉字测试\n");
    wprint(L"  Ext-A:     㐀㐁㐂 (U+3400)\n");
    wprint(L"  Ext-B:     𠀀𠀁𠀂 (U+20000, 需 surrogate pair)\n");

    wprint(L"\n  ── 双宽字符对齐 ──\n");
    wprint(L"  ┌────────────────────────────┐\n");
    wprint(L"  │ 姓名      │ 测试用户       │\n");
    wprint(L"  │ 日期      │ 2026-05-22     │\n");
    wprint(L"  │ 说明      │ CJK对齐测试    │\n");
    wprint(L"  └────────────────────────────┘\n");

    wprint(L"\n  ── 宽字符 + 窄字符混合对齐 ──\n");
    wprint(L"  AAA     中  B\n");
    wprint(L"  BBB    文文 C\n");
    wprint(L"  CCC   测试  D\n");
    wprint(L"  期望: 中文列对齐, 英文列也对齐\n");

    pause_enter(L"检查 CJK 对齐 (每个汉字占 2 个英文宽度)");
}

// ═══════════════════════════════════════════════════════
// 7. 零宽字符 — ZWJ/ZWNJ/ZWSP/WJ
// ═══════════════════════════════════════════════════════
void test_zero_width()
{
    section(L"7. 零宽字符 — ZWJ / ZWNJ / ZWSP");

    wprint(L"  ZWJ (U+200D) — 零宽连接符: 连接相邻字符形成合字\n");
    wprint(L"  👨 + ZWJ + 💻 = 👨\u200D💻 (男程序员)\n");
    wprint(L"  👩 + ZWJ + 🎓 = 👩\u200D🎓 (女毕业生)\n");

    wprint(L"\n  ZWNJ (U+200C) — 零宽断连符: 阻止连写\n");
    wprint(L"  波斯语: ﻧﻤﯽ\u200Cخواﻫﻢ (中间的 ZWNJ 阻止字母连写)\n");

    wprint(L"\n  ZWSP (U+200B) — 零宽空格: 不可见但可在此处换行\n");
    wprint(L"  长单词隐式断词点: Super\u200BCali\u200BFragi\u200Blistic\n");

    wprint(L"\n  WJ (U+2060) — 词连接符: 禁止在此处换行\n");
    wprint(L"  不\u2060可\u2060分\u2060割  (不应在中间断行)\n");

    pause_enter(L"检查零宽字符效果 (不影响宽度, 但影响字形/换行)");
}

// ═══════════════════════════════════════════════════════
// 8. Tab 与 Unicode
// ═══════════════════════════════════════════════════════
void test_tab_unicode()
{
    section(L"8. Tab 停止位 + Unicode");

    wprint(L"  Tab 位 (每 8 列):\n");
    wprint(L"  英\t中\t英中\t测试\tTab\t对齐\t!\n");
    wprint(L"  A\t文\tAB\t中文\t123\t한글\tX\n");

    wprint(L"\n  期望: 每个 Tab 后的文本对齐到 8 的倍数列,\n");
    wprint(L"  考虑中文字符的双宽特性\n");

    pause_enter(L"检查 Tab 对齐 (含中文, 每8列一个停止位)");
}

// ═══════════════════════════════════════════════════════
// 9. 数学 + 技术符号
// ═══════════════════════════════════════════════════════
void test_math_symbols()
{
    section(L"9. 数学 + 技术符号");

    wprint(L"  数学字母:\n  𝐀𝐁𝐂𝐝𝐞𝐟 𝒜𝒞𝒟 ℊℋℌ ℂℕℚℝ\n");
    wprint(L"  运算符:  ∀ ∃ ∄ ∅ ∈ ∉ ∋ ∌ ∏ ∐ ∑ − ∓ ∔ ∕ ∖\n");
    wprint(L"  箭头:    ← ↑ → ↓ ↔ ↕ ↖ ↗ ↘ ↙\n");
    wprint(L"  几何:    ∠ ∡ ∢ ∥ ∦ ∧ ∨ ∩ ∪ ∫ ∬ ∮\n");

    wprint(L"\n  上标/下标 (U+2070-U+209F):\n");
    wprint(L"  x² + y² = r²    H₂O    E=mc²\n");

    wprint(L"\n  分数 (U+2150-U+218F):\n");
    wprint(L"  ½ ⅓ ⅔ ¼ ¾ ⅕ ⅖ ⅗ ⅘ ⅙ ⅚ ⅛ ⅜ ⅝ ⅞\n");

    wprint(L"\n  罗马数字 (U+2160-U+217F):\n");
    wprint(L"  Ⅰ Ⅱ Ⅲ Ⅳ Ⅴ Ⅵ Ⅶ Ⅷ Ⅸ Ⅹ Ⅺ Ⅻ\n");

    pause_enter(L"检查数学符号是否正确");
}

// ═══════════════════════════════════════════════════════
// 10. 变体选择符 (Variation Selectors)
// ═══════════════════════════════════════════════════════
void test_variation_selectors()
{
    section(L"10. 变体选择符 VS15/VS16 — Text/Emoji 风格");

    wprint(L"  VS15 (U+FE0E) = Text 风格, VS16 (U+FE0F) = Emoji 风格:\n");
    wprint(L"  ©  (默认)  vs  ©\uFE0E (Text)  vs  ©\uFE0F (Emoji)\n");
    wprint(L"  ®  (默认)  vs  ®\uFE0E (Text)  vs  ®\uFE0F (Emoji)\n");
    wprint(L"  ‼  (默认)  vs  ‼\uFE0E (Text)  vs  ‼\uFE0F (Emoji)\n");
    wprint(L"  ❤ (默认)   vs  ❤\uFE0E (Text)  vs  ❤\uFE0F (Emoji)\n");

    wprint(L"\n  VS1-VS14 (U+FE00-U+FE0D) — 蒙古文变体:\n");
    wprint(L"  ᠠ\uFE00 ᠠ\uFE01 ᠠ\uFE02 (蒙古文变体选择)\n");

    pause_enter(L"检查变体选择符 (Text vs Emoji 风格差异)");
}

// ═══════════════════════════════════════════════════════
// 11. 换行/不换行规则 — 软连字符 / 不间断空格
// ═══════════════════════════════════════════════════════
void test_line_break()
{
    section(L"11. 换行控制字符");

    wprint(L"  SHY (U+00AD) — 软连字符: 仅在断行时显示:\n");
    wprint(L"  长单词\u00AD断行\u00AD测试\u00AD点 (行末不应显示连字符)\n");

    wprint(L"  NBSP (U+00A0) — 不间断空格: 阻止断行:\n");
    wprint(L"  数字\u00A0100\u00A0万 (不应在中间断行)\n");

    wprint(L"  NNBSP (U+202F) — 窄不间断空格:\n");
    wprint(L"  1\u202F000\u202F000 (数字分组, 法文习惯)\n");

    pause_enter(L"检查换行行为 (在窄终端中缩小窗口观察)");
}

// ═══════════════════════════════════════════════════════
// 12. 非打印字符可视化
// ═══════════════════════════════════════════════════════
void test_control_pictures()
{
    section(L"12. 控制字符图形 (U+2400-U+243F)");

    wprint(L"  控制字符的可见表示:\n");
    wprint(L"  NUL=\u2400  SOH=\u2401  STX=\u2402  ETX=\u2403\n");
    wprint(L"  BEL=\u2407  BS=\u2408   HT=\u2409   LF=\u240A\n");
    wprint(L"  CR=\u240D  ESC=\u241B  DEL=\u2421  SP=\u2420\n");

    wprint(L"\n  这些符号用于可视化不可打印的 ASCII 控制字符\n");

    pause_enter(L"检查控制字符图形");
}

// ═══════════════════════════════════════════════════════
// 13. 特殊空白字符
// ═══════════════════════════════════════════════════════
void test_spaces()
{
    section(L"13. Unicode 各种空白字符");

    wprint(L"  各种宽度的空格 (标记为 ░...░):\n");
    wprint(L"  SP  (U+0020):  ' '\n");
    wprint(L"  NBSP(U+00A0):  '\u00A0'\n");
    wprint(L"  NNBSP(U+202F): '\u202F'\n");
    wprint(L"  MMSP(U+205F):  '\u205F'\n");
    wprint(L"  EMSP(U+2003):  '\u2003'\n");
    wprint(L"  ENSP(U+2002):  '\u2002'\n");
    wprint(L"  TS  (U+2009):  '\u2009'\n");
    wprint(L"  HS  (U+200A):  '\u200A'\n");

    wprint(L"\n  IDSP(U+3000) — 表意空格 (全角):\n");
    wprint(L"  前'　'后  (前后应有全角空格)\n");

    pause_enter(L"检查不同宽度的空格字符");
}

// ═══════════════════════════════════════════════════════
// 14. 古文字 + 罕见文字
// ═══════════════════════════════════════════════════════
void test_ancient_scripts()
{
    section(L"14. 古文字 + 罕见文字 (若字体支持)");

    wprint(L"  埃及圣书体 (U+13000):  𓀀𓀁𓀂𓀃 (可能豆腐块)\n");
    wprint(L"  楔形文字 (U+12000):    𒀀𒀁𒀂 (苏美尔语)\n");
    wprint(L"  线形文字 B (U+10000):  𐀀𐀁𐀂 (迈锡尼希腊语)\n");
    wprint(L"  哥特体 (U+10330):      𐍐𐍑𐍒𐍓𐍔\n");
    wprint(L"  如尼文 (U+16A0):       ᚠᚡᚢᚣᚤ\n");
    wprint(L"  Deseret (U+10400):     𐐀𐐁𐐂𐐃\n");

    wprint(L"\n  注: 古文字可能因字体缺失显示为豆腐块 (□ 或 �),\n");
    wprint(L"  这不代表 ConPTY 的 bug.\n");

    pause_enter(L"检查古文字 (取决于终端字体)");
}

// ═══════════════════════════════════════════════════════
// 15. 代理对 + 补充平面综合
// ═══════════════════════════════════════════════════════
void test_supplementary()
{
    section(L"15. 补充平面字符 (U+10000 以上, Surrogate Pairs)");

    wprint(L"  UTF-16 代理对 — WriteConsoleW 必须正确处理:\n");
    wprint(L"  U+1F600 (😀) = D83D DE00\n");
    wprint(L"  U+1F4A9 (💩) = D83D DCA9\n");
    wprint(L"  U+1F98A (🦊) = D83E DD8A\n");

    wprint(L"\n  逐字列出 (每个补充平面字符占 2 个 wchar_t):\n");
    wprint(L"  😀😃😄😁😅😂🤣😊😇🙂🙃😉😌😍🥰😘\n");

    wprint(L"\n  补充平面汉字 (CJK Ext-B, U+20000+):\n");
    wprint(L"  𠀀𠀁𠀂𠀃𠀄𠀅𠀆𠀇𠀈𠀉𠀊\n");

    wprint(L"\n  音乐符号 (U+1D100+):\n");
    wprint(L"  𝄞 (G 谱号)  𝄢 (F 谱号)  𝄫 (降号)  𝄪 (重升号)\n");

    pause_enter(L"检查补充平面字符 (Emoji + Ext-B 汉字)");
}

// ═══════════════════════════════════════════════════════
// 16. 合字 — 连字 (Ligatures) 效果
// ═══════════════════════════════════════════════════════
void test_ligatures()
{
    section(L"16. 合字 / 连字 (Ligatures) — 字体级效果");

    wprint(L"  以下字符组合在支持连字的字体中会合并:\n");
    wprint(L"  fi  fl  ffi  ffl  (拉丁连字, 常见于 serif 字体)\n");
    wprint(L"  -->  ==>  !=  :=  (编程连字, Fira Code / Cascadia Code)\n");
    wprint(L"  ::  ->  =>  <=  >=  |>  <|>  (函数式编程符号)\n");

    wprint(L"\n  注: 连字是终端/字体的渲染特性, 不是 Unicode 层面.\n");
    wprint(L"  ConPTY 只需正确传递字符, 渲染由终端完成.\n");

    pause_enter(L"如果使用 Cascadia Code 等字体, 检查连字效果");
}

// ═══════════════════════════════════════════════════════
// 17. 彩色字体的 emoji + 文本混合
// ═══════════════════════════════════════════════════════
void test_mixed_emoji_text()
{
    section(L"17. Emoji + 文本混合渲染");

    wprint(L"  混合行内 Emoji 和文字:\n");
    wprint(L"  今天天气真好 ☀️, 温度 25°C, 适合 🏃‍♂️ 跑步!\n");
    wprint(L"  🍕 + 🍔 = 😋, 但 🥗 更健康.\n");
    wprint(L"  编程 💻 + 咖啡 ☕ = 🚀 生产力\n");

    wprint(L"\n  Emoji + 中文:\n");
    wprint(L"  ✅ 任务完成  ❌ 测试失败  ⚠️ 警告  ℹ️ 信息\n");

    wprint(L"\n  Emoji + 日文:\n");
    wprint(L"  🗾 日本 🇯🇵  🗼 東京 🗾\n");

    pause_enter(L"检查 Emoji 与文字混合时对齐和颜色是否正确");
}

// ═══════════════════════════════════════════════════════
// 18. 长文本 + Unicode 换行正确性
// ═══════════════════════════════════════════════════════
void test_long_text_wrap()
{
    section(L"18. 长文本换行 — Unicode 行宽计算");

    wprint(L"  纯 ASCII 80 列边界 — 应恰好填满一行:\n");
    for (int i = 0; i < 80; ++i)
        wprint(L"=");
    wprint(L"\n(上面一行应有 80 个 '=' 恰好填满)\n");

    wprint(L"\n  中文 40 字 = 80 列: (每个汉字占 2 列)\n");
    for (int i = 0; i < 40; ++i)
        wprint(L"测");
    wprint(L"\n(上面一行应有 40 个 '测', 恰好填满 80 列)\n");

    wprint(L"\n  中英混合 80 列:\n");
    wprint(L"  AAA中文BBB测试CCC文字DDD混合 → 应填满 80 列后自动换行...");
    for (int i = 0; i < 60; ++i)
        wprint(i % 2 ? L"中" : L"A");
    wprint(L"\n(上面一行混合 A 和中, 期望换行位置正确)\n");

    pause_enter(L"检查换行边界是否准确 (中英文混合宽度)");
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

    vt(L"\x1b[2J\x1b[H");
    section(L"ConPTY Unicode 功能手动验证程序");
    wprint(L"  本程序测试各种 Unicode 特性, 请肉眼观察渲染结果.\n");
    wprint(L"  包括: 多语言文字、组合字符、Emoji、RTL、CJK 对齐等.\n");

    test_bmp_languages();
    test_combining();
    test_emoji_basic();
    test_emoji_flags();
    test_rtl();
    test_cjk();
    test_zero_width();
    test_tab_unicode();
    test_math_symbols();
    test_variation_selectors();
    test_line_break();
    test_control_pictures();
    test_spaces();
    test_ancient_scripts();
    test_supplementary();
    test_ligatures();
    test_mixed_emoji_text();
    test_long_text_wrap();

    section(L"全部 Unicode 测试完成!");
    wprint(L"  ✓ 无豆腐块 → 字体支持良好\n");
    wprint(L"  ✓ 组合字符叠放正确 → ConPTY 字符传递完整\n");
    wprint(L"  ✓ RTL 从右向左 → Bidi 算法正常\n");
    wprint(L"  ✓ CJK 双宽对齐 → 列宽计算正确\n\n");
    return 0;
}
