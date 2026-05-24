// uni_010_vs.cpp — 变体选择符 VS15/VS16
#include "manual_common.hpp"

int main()
{
    HANDLE hOut = init_console();
    title(L"uni-010",
          L"变体选择符 VS15/VS16 — Text/Emoji 风格\n   测试 VS15 (U+FE0E, Text 风格) 和 VS16 (U+FE0F, Emoji 风格)。\n  "
          L" 期望：VS15 强制黑白文本风格，VS16 强制彩色 Emoji 风格。\n   对比同一个基础字符在三种风格下的显示差异。");

    wprint(L"\x1b[1;32m[期望结果]\x1b[0m\n");
    wprint(L"  每个符号显示三列:\n");
    wprint(L"    默认: 取决于终端\n");
    wprint(L"    VS15: Text 风格 (黑白, 细线)\n");
    wprint(L"    VS16: Emoji 风格 (彩色, 粗体)\n");

    sep();
    wprint(L"\x1b[1;32m[实际输出]\x1b[0m\n\n");

    wprint(L"  Char   默认    VS15(Text)    VS16(Emoji)\n");
    wprint(L"  ─────  ──────  ────────────  ───────────\n");
    wprint(L"  ©      ©        ©\uFE0E           ©\uFE0F\n");
    wprint(L"  ®      ®        ®\uFE0E           ®\uFE0F\n");
    wprint(L"  ‼      ‼        ‼\uFE0E           ‼\uFE0F\n");
    wprint(L"  ❤      ❤       ❤\uFE0E          ❤\uFE0F\n");
    wprint(L"  ✖      ✖       ✖\uFE0E          ✖\uFE0F\n");
    wprint(L"  ➡      ➡       ➡\uFE0E          ➡\uFE0F\n");

    wprint(L"\n  \x1b[1;37m验证:\x1b[0m VS15 列应为 Text 风格 (可能黑白), VS16 列 Emoji 风格 (彩色)\n");

    wait3s(L"检查 VS15 Text 风格 vs VS16 Emoji 风格差异");
    return 0;
}
