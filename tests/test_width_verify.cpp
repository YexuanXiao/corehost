#include "char_width.hpp"
#include "text_measurement_mode.hpp"
#include <iostream>
#include <cstdlib>
#include <string>
#include <iomanip>

int main()
{
    using namespace conpty;

    // ── Part 1: 单字符宽度验�?──
    std::cout << "=== Part 1: Single Char Width ===" << std::endl;
    struct
    {
        char32_t cp;
        const char *name;
    } cases[] = {
        {U'\x3001', "U+3001 IDEOGRAPHIC COMMA"},
        {U'\x3002', "U+3002 IDEOGRAPHIC FULL STOP"},
        {U'\xFF0C', "U+FF0C FULLWIDTH COMMA"},
    };

    bool all_pass = true;
    for (auto [cp, name] : cases)
    {
        int w_console = char_width_console(cp);
        int w_wcswidth = char_width_wcswidth(cp);
        int w_unicode = char_width_unicode(cp);
        int w_graphemes = char_width_for_mode(cp, text_measurement_mode::graphemes, false);
        int w_g_ambig = char_width_for_mode(cp, text_measurement_mode::graphemes, true);

        std::cout << name << ":" << std::endl;
        std::cout << "  console:   " << w_console << (w_console == 2 ? " OK" : " FAIL") << std::endl;
        std::cout << "  wcswidth:  " << w_wcswidth << (w_wcswidth == 2 ? " OK" : " FAIL") << std::endl;
        std::cout << "  unicode:   " << w_unicode << (w_unicode == 2 ? " OK" : " FAIL") << std::endl;
        std::cout << "  graphemes: " << w_graphemes << (w_graphemes == 2 ? " OK" : " FAIL") << std::endl;
        std::cout << "  g+ambi:    " << w_g_ambig << (w_g_ambig == 2 ? " OK" : " FAIL") << std::endl;
        std::cout << std::endl;

        if (w_console != 2 || w_wcswidth != 2 || w_unicode != 2 || w_graphemes != 2 || w_g_ambig != 2)
            all_pass = false;
    }

    // ── Part 2: PS error msg full-string width ──
    std::cout << "=== Part 2: PS Error Message Full-String Width ===" << std::endl;
    std::u32string msg = U"wrong_command : 无法将\"wrong_command\"项识别为 "
                         U"cmdlet、函数、脚本文件或可运行程序的名称。请检查名称的拼写，如果包括路径，";
    std::cout << "String length (code points): " << msg.size() << std::endl;

    int tw_console = 0;
    int tw_wcswidth = 0;
    int tw_unicode = 0;
    for (char32_t cp : msg)
    {
        tw_console += char_width_console(cp);
        tw_wcswidth += char_width_wcswidth(cp);
        tw_unicode += char_width_unicode(cp);
    }
    int tw_graphemes = text_width_for_mode(msg, text_measurement_mode::graphemes, false);
    int tw_g_ambig = text_width_for_mode(msg, text_measurement_mode::graphemes, true);

    std::cout << "Total widths:" << std::endl;
    std::cout << "  console:       " << tw_console << std::endl;
    std::cout << "  wcswidth:      " << tw_wcswidth << std::endl;
    std::cout << "  unicode:       " << tw_unicode << std::endl;
    std::cout << "  graphemes:     " << tw_graphemes << std::endl;
    std::cout << "  graphemes+ambi:" << tw_g_ambig << std::endl;

    // ── Part 3: Per-char breakdown (compact) ──
    std::cout << std::endl << "=== Part 3: Per-Char Breakdown ===" << std::endl;
    std::cout << "idx   codepoint  console wcswidth  unicode  char" << std::endl;
    for (size_t i = 0; i < msg.size(); ++i)
    {
        char32_t cp = msg[i];
        int wc = char_width_console(cp);
        int ww = char_width_wcswidth(cp);
        int wu = char_width_unicode(cp);

        // 只在宽度�?时打印（节省输出�?
        if (wc != 1 || ww != 1 || wu != 1)
        {
            char utf8[8] = {};
            if (cp < 0x80)
            {
                utf8[0] = static_cast<char>(cp);
            }
            else if (cp < 0x800)
            {
                utf8[0] = 0xC0 | (cp >> 6);
                utf8[1] = 0x80 | (cp & 0x3F);
            }
            else if (cp < 0x10000)
            {
                utf8[0] = 0xE0 | (cp >> 12);
                utf8[1] = 0x80 | ((cp >> 6) & 0x3F);
                utf8[2] = 0x80 | (cp & 0x3F);
            }
            else
            {
                utf8[0] = 0xF0 | (cp >> 18);
                utf8[1] = 0x80 | ((cp >> 12) & 0x3F);
                utf8[2] = 0x80 | ((cp >> 6) & 0x3F);
                utf8[3] = 0x80 | (cp & 0x3F);
            }

            std::cout << std::setw(3) << i << "   U+" << std::hex << std::setw(4) << std::setfill('0')
                      << static_cast<uint32_t>(cp) << std::dec << std::setfill(' ') << "   " << wc << "        " << ww
                      << "         " << wu << "      " << utf8 << std::endl;
        }
    }

    std::cout << std::endl;
    std::cout << (all_pass ? "ALL PASS" : "SOME FAILED") << std::endl;
    return all_pass ? 0 : 1;
}
