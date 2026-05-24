// uni_016_ligatures.cpp — 连字 Ligatures
#include "manual_common.hpp"

int main()
{
    HANDLE hOut = init_console();
    title(L"uni-016",
          L"合字 / 连字 (Ligatures) — 字体级效果\n   测试终端字体对连字的支持。\n   期望：如果终端使用连字字体 (如 "
          L"Cascadia Code, Fira Code),\n   以下字符组合会合并显示：fi→f i 连写, fl→f l 连写,\n   --> → 箭头合并, != → "
          L"不等号合并。\n   无连字字体时无效果。这是字体渲染特性, 非 ConPTY 功能。");

    wprint(L"\x1b[1;32m[期望结果]\x1b[0m\n");
    wprint(L"  有连字字体: 特定组合合并为一个连字字形\n");
    wprint(L"  无连字字体: 字符独立显示 (正常)\n");

    sep();
    wprint(L"\x1b[1;32m[实际输出]\x1b[0m\n\n");

    wprint(L"  ── 拉丁连字 (传统印刷) ──\n");
    wprint(L"  fi fl ffi ffl\n");
    wprint(L"  ff ft st ct\n");

    wprint(L"\n  ── 编程连字 (Fira Code / Cascadia Code) ──\n");
    wprint(L"  ->  -->  ==>  !=  :=  <=  >=  <>\n");
    wprint(L"  ::  =>  |>  <|>  ||  |>\n");
    wprint(L"  ++  --  **  ///  ===\n");

    wprint(L"\n  \x1b[1;37m注:\x1b[0m 连字是字体特性, ConPTY 只负责传递字符\n");

    wait3s(L"如果使用 Cascadia Code, 检查编程连字效果");
    return 0;
}
