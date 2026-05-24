// vt_001_cursor.cpp — CUP/CHA/VPA/CNL/CPL/CUF/CUB/CUU/CUD
#include "manual_common.hpp"

int main()
{
    HANDLE hOut = init_console();
    title(L"vt-001",
          L"光标定位 — CUP/CHA/VPA/CNL/CPL/CUF/CUB/CUU/CUD\n   测试全部 VT 光标移动序列。期望：\n   CUP(5,1): "
          L"光标跳到第1行第5列\n   CUP(20,3): 光标跳到第3行第20列\n   CHA(40): 光标水平移到第40列，行不变\n   VPA(5): "
          L"光标垂直到第5行，列不变\n   CNL(2): 下移2行到行首\n   CPL(1): 上移1行到行首\n   CUF(10): 右移10列\n   "
          L"CUB(5): 左移5列\n   CUU(2): 上移2行\n   CUD(3): 下移3行");

    wprint(L"\x1b[1;32m[实际输出]\x1b[0m\n\n");
    wprint(L"  [CUP (5,1)] ");
    vt(L"\x1b[1;5H");
    wprint(L"← 应在(5,1)");
    Sleep(800);
    wprint(L"\n  [CUP (20,3)] ");
    vt(L"\x1b[3;20H");
    wprint(L"← 应在(20,3)");
    Sleep(800);
    wprint(L"\n  [CHA 40] ");
    vt(L"\x1b[40G");
    wprint(L"← 应在列40");
    Sleep(800);
    wprint(L"\n  [VPA 5] ");
    vt(L"\x1b[5d");
    wprint(L"← 应在行5");
    Sleep(800);
    wprint(L"\n  [CNL 2] ");
    vt(L"\x1b[2E");
    wprint(L"← 下移2行,行首");
    Sleep(800);
    wprint(L"\n  [CPL 1] ");
    vt(L"\x1b[1F");
    wprint(L"← 上移1行,行首");
    Sleep(800);
    wprint(L"\n  [CUF 10] ");
    vt(L"\x1b[10C");
    wprint(L"← 右移10列");
    Sleep(800);
    wprint(L"\n  [CUB 5] ");
    vt(L"\x1b[5D");
    wprint(L"← 左移5列");
    Sleep(800);
    wprint(L"\n  [CUU 2] ");
    vt(L"\x1b[2A");
    wprint(L"← 上移2行");
    Sleep(800);
    wprint(L"\n  [CUD 3] ");
    vt(L"\x1b[3B");
    wprint(L"← 下移3行");

    wait3s(L"检查每个序列后的光标位置标记");
    return 0;
}
