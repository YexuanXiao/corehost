// vt_009_su_sd_il_dl.cpp — SU/SD 滚动 + IL/DL 插入/删除行
#include "manual_common.hpp"

int main()
{
    HANDLE hOut = init_console();
    title(L"vt-009", L"SU/SD 滚动 + IL/DL 插入/删除行\n   测试 ESC[Ps S (SU 向上滚动), ESC[Ps T (SD 向下滚动),\n   "
                     L"ESC[Ps L (IL 插入行), ESC[Ps M (DL 删除行)。\n   期望：SU 内容向下滚2行，SD 内容向上滚2行。\n   "
                     L"IL 插入2空白行，DL 删除刚插入的2行。");

    wprint(L"\x1b[1;32m[期望结果]\x1b[0m\n");
    wprint(L"  SU(2): 内容滚下2行 (下方出现空白)\n");
    wprint(L"  SD(2): 内容滚上2行 (恢复)\n");
    wprint(L"  IL(2): 当前行及以下下移2行 (插入空白行)\n");
    wprint(L"  DL(2): 当前行及以下上移2行 (删除)\n");

    sep();
    wprint(L"\x1b[1;32m[实际输出]\x1b[0m\n\n");

    wprint(L"  [SU 2] 向上滚动 2 行 → ");
    vt(L"\x1b[2S");
    wprint(L"内容下滚2行\n");
    wprint(L"  [SD 2] 向下滚动 2 行 → ");
    vt(L"\x1b[2T");
    wprint(L"内容上滚2行 (恢复)\n");

    wprint(L"  [IL 2] 插入 2 行 → ");
    vt(L"\x1b[2L");
    wprint(L"当前行下移, 2 空白行插入\n");
    wprint(L"  [DL 2] 删除 2 行 → ");
    vt(L"\x1b[2M");
    wprint(L"删除刚插入的行\n");

    wait3s(L"检查 SU/SD 滚动和 IL/DL 插入删除效果");
    return 0;
}
