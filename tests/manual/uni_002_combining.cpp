// uni_002_combining.cpp — 组合字符
#include "manual_common.hpp"

int main()
{
    HANDLE hOut = init_console();
    title(L"uni-002", L"组合字符 — 基础字母 + 附加符号\n   测试 Unicode 组合字符 (Combining Diacritical Marks)。\n   "
                      L"期望：基础字母 + 组合符号应叠放为一个完整的带音调字符。\n   例如 e + U+0301 应显示为 "
                      L"é。多重组合同理。\n   覆盖：拉丁、希腊、西里尔组合。");

    wprint(L"\x1b[1;32m[期望结果]\x1b[0m\n");
    wprint(L"  每个 '=' 左边是组合序列, 右边是对应的预期字形\n");
    wprint(L"  例如: e + ◌́ → é, a + ◌̂ → â\n");

    sep();
    wprint(L"\x1b[1;32m[实际输出]\x1b[0m\n\n");

    wprint(L"  ── 拉丁基本组合 ──\n");
    wprint(L"  e + ◌́  = e\u0301  (应显示 é)\n");
    wprint(L"  a + ◌̂  = a\u0302  (应显示 â)\n");
    wprint(L"  o + ◌̈  = o\u0308  (应显示 ö)\n");
    wprint(L"  n + ◌̃  = n\u0303  (应显示 ñ)\n");
    wprint(L"  c + ◌̧  = c\u0327  (应显示 ç)\n");
    wprint(L"  u + ◌̆  = u\u0306  (应显示 ŭ)\n");

    wprint(L"\n  ── 多重组合 ──\n");
    wprint(L"  a + ◌̂ + ◌̃ = a\u0302\u0303 (ẫ, 越南语)\n");
    wprint(L"  A + ◌̊ = A\u030A (Å 的 NFD 形式)\n");

    wprint(L"\n  ── 希腊/西里尔组合 ──\n");
    wprint(L"  α + ◌̓ + ◌́ = α\u0313\u0301 (ἄ)\n");
    wprint(L"  и + ◌̆ = и\u0306 (й 的分解)\n");

    wait3s(L"检查组合符号是否叠放在字母上方/下方");
    return 0;
}
