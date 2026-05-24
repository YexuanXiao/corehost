// uni_001_bmp.cpp — 基本多语言面各语言文字
#include "manual_common.hpp"

int main()
{
    HANDLE hOut = init_console();
    title(L"uni-001",
          L"基本多语言面 (BMP) — 各语言文字\n   测试 12 种语言/文字系统的字符显示。\n   "
          L"期望：每种文字应正确渲染，不出现豆腐块 (□) 或乱码。\n   "
          L"覆盖：ASCII、拉丁扩展、希腊、西里尔、中文、日文、韩文、\n   泰文、阿拉伯文、希伯来文、天城文、越南文。");

    wprint(L"\x1b[1;32m[期望结果]\x1b[0m\n");
    wprint(L"  12 行, 每行一种语言文字\n");
    wprint(L"  所有字符应有正确的字形, 无 □ 或 �\n");

    sep();
    wprint(L"\x1b[1;32m[实际输出]\x1b[0m\n\n");

    wprint(L"  ASCII:        Hello, World! 12345\n");
    wprint(L"  拉丁扩展:     é à ö ñ ç ß Å Ø\n");
    wprint(L"  希腊字母:     ΑΒΓΔΕ αβγδε\n");
    wprint(L"  西里尔:       Привет, мир! (俄语'你好世界')\n");
    wprint(L"  中文 (CJK):    你好世界！汉字测试\n");
    wprint(L"  日文:         こんにちは世界！\n");
    wprint(L"  韩文:         안녕하세요 세계!\n");
    wprint(L"  泰文:         สวัสดีชาวโลก\n");
    wprint(L"  阿拉伯文:     مرحبا بالعالم (RTL)\n");
    wprint(L"  希伯来文:     שלום עולם (RTL)\n");
    wprint(L"  天城文:       नमस्ते दुनिया\n");
    wprint(L"  越南文:       Xin chào thế giới\n");

    wait3s(L"检查 12 种语言显示无豆腐块");
    return 0;
}
