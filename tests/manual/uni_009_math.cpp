// uni_009_math.cpp — 数学 + 技术符号
#include "manual_common.hpp"

int main()
{
    HANDLE hOut = init_console();
    title(L"uni-009", L"数学 + 技术符号\n   测试数学字母数字符号 (U+1D400+)、运算符 (U+2200+)、\n   箭头 "
                      L"(U+2190+)、上标/下标、分数、罗马数字。\n   期望：数学粗体/斜体字母、常用运算符、各类箭头、\n   "
                      L"分数和罗马数字均正确显示。");

    wprint(L"\x1b[1;32m[期望结果]\x1b[0m\n");
    wprint(L"  数学粗体: 𝐀𝐁𝐂𝐝𝐞𝐟\n");
    wprint(L"  运算符:   ∀ ∃ ∅ ∈ ∉ ∑ ∏ ∫\n");
    wprint(L"  箭头:     ← ↑ → ↓ ↔\n");
    wprint(L"  上标/下标: x² y³ H₂O\n");
    wprint(L"  分数:     ½ ⅓ ⅔ ¼ ¾\n");
    wprint(L"  罗马数字: Ⅰ Ⅱ Ⅲ Ⅳ Ⅴ\n");

    sep();
    wprint(L"\x1b[1;32m[实际输出]\x1b[0m\n\n");

    wprint(L"  数学字母符号:\n  𝐀𝐁𝐂𝐝𝐞𝐟 𝒜𝒞𝒟 ℊℋℌ ℂℕℚℝ\n");
    wprint(L"  运算符:\n  ∀ ∃ ∄ ∅ ∈ ∉ ∋ ∌ ∏ ∐ ∑ − ∓ ∔ ∕ ∖\n");
    wprint(L"  箭头:\n  ← ↑ → ↓ ↔ ↕ ↖ ↗ ↘ ↙\n");
    wprint(L"  上标/下标:\n  x² + y² = r²    H₂O    E=mc²\n");
    wprint(L"  分数:\n  ½ ⅓ ⅔ ¼ ¾ ⅕ ⅖ ⅗ ⅘ ⅙ ⅚ ⅛ ⅜ ⅝ ⅞\n");
    wprint(L"  罗马数字:\n  Ⅰ Ⅱ Ⅲ Ⅳ Ⅴ Ⅵ Ⅶ Ⅷ Ⅸ Ⅹ Ⅺ Ⅻ\n");

    wait3s(L"检查数学符号正确渲染");
    return 0;
}
