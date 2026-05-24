// ── tests/test_char_convert.cpp ─────────────────────────
// 编码转换单元测试 (char_convert.hpp, libunicode)
//
// 覆盖: UTF-16↔UTF-32, UTF-8↔UTF-32, ANSI→UTF-32,
//       utf8_stream_decoder, resize_and_overwrite 持久缓冲
#include "test_common.hpp"
#include "conpty/char_convert.hpp"
#include <string>
#include <string_view>
#include <vector>

using namespace conpty;

// ═══════════════════════════════════════════════════════
// UTF-16 → UTF-32 (convert_utf16_to_u32)
// ═══════════════════════════════════════════════════════

bool test_utf16_to_u32_bmp()
{
    // BMP 字符: 1:1 映射
    std::u32string out;
    std::wstring ws = L"Hello World!";
    convert_utf16_to_u32(ws, out);
    ASSERT(out.size() == ws.size());
    ASSERT(out[0] == U'H');
    ASSERT(out[1] == U'e');
    ASSERT(out[11] == U'!');
    return true;
}

bool test_utf16_to_u32_empty()
{
    std::u32string out = U"garbage";
    std::wstring ws;
    convert_utf16_to_u32(ws, out);
    ASSERT(out.empty());
    return true;
}

bool test_utf16_to_u32_surrogate()
{
    // 非BMP字符: 代理对 2→1
    // U+1F600 (😀) = surrogate pair U+D83D U+DE00
    std::u32string out;
    std::wstring ws = L"a\U0001F600z";
    convert_utf16_to_u32(ws, out);
    ASSERT(out.size() == 3); // a + 😀 + z = 3 个 char32_t
    ASSERT(out[0] == U'a');
    ASSERT(out[1] == 0x1F600);
    ASSERT(out[2] == U'z');
    return true;
}

bool test_utf16_to_u32_large()
{
    // 大输入: 验证 resize_and_overwrite 正确
    std::u32string out;
    std::wstring ws(10000, L'X');
    convert_utf16_to_u32(ws, out);
    ASSERT(out.size() == ws.size());
    ASSERT(out[0] == U'X');
    ASSERT(out[9999] == U'X');
    return true;
}

bool test_utf16_to_u32_persistent()
{
    // 验证持久缓冲复用: 多次写入不应泄漏或错位
    std::u32string out;
    std::wstring ws1 = L"AAA";
    std::wstring ws2 = L"BB";
    convert_utf16_to_u32(ws1, out);
    ASSERT(out.size() == 3);
    convert_utf16_to_u32(ws2, out);
    ASSERT(out.size() == 2);
    ASSERT(out[0] == U'B');
    return true;
}

// ═══════════════════════════════════════════════════════
// UTF-32 → UTF-16 (convert_u32_to_wstr)
// ═══════════════════════════════════════════════════════

bool test_u32_to_wstr_bmp()
{
    std::wstring out;
    std::u32string_view u32s = U"Test";
    convert_u32_to_wstr(u32s, out);
    ASSERT(out.size() == 4);
    ASSERT(out == L"Test");
    return true;
}

bool test_u32_to_wstr_surrogate()
{
    // U+1F600 → surrogate pair U+D83D U+DE00
    std::wstring out;
    char32_t cp = 0x1F600;
    convert_u32_to_wstr(std::u32string_view{&cp, 1}, out);
    ASSERT(out.size() == 2);
    ASSERT(out[0] == 0xD83D);
    ASSERT(out[1] == 0xDE00);
    return true;
}

bool test_u32_to_wstr_empty()
{
    std::wstring out = L"garbage";
    std::u32string_view u32s;
    convert_u32_to_wstr(u32s, out);
    ASSERT(out.empty());
    return true;
}

// ═══════════════════════════════════════════════════════
// UTF-8 → UTF-32 (convert_utf8_to_u32)
// ═══════════════════════════════════════════════════════

bool test_utf8_to_u32_ascii()
{
    std::u32string out;
    std::string_view utf8 = "Hello";
    convert_utf8_to_u32(utf8, out);
    ASSERT(out.size() == 5);
    ASSERT(out[0] == U'H');
    return true;
}

bool test_utf8_to_u32_2byte()
{
    // U+00E9 (é) = UTF-8: 0xC3 0xA9
    std::u32string out;
    std::string utf8 = "\xC3\xA9";
    convert_utf8_to_u32(std::string_view{utf8}, out);
    ASSERT(out.size() == 1);
    ASSERT(out[0] == 0x00E9);
    return true;
}

bool test_utf8_to_u32_3byte()
{
    // U+4E2D (中) = UTF-8: 0xE4 0xB8 0xAD
    std::u32string out;
    std::string utf8 = "\xE4\xB8\xAD";
    convert_utf8_to_u32(std::string_view{utf8}, out);
    ASSERT(out.size() == 1);
    ASSERT(out[0] == 0x4E2D);
    return true;
}

bool test_utf8_to_u32_4byte()
{
    // U+1F600 (😀) = UTF-8: 0xF0 0x9F 0x98 0x80
    std::u32string out;
    std::string utf8 = "\xF0\x9F\x98\x80";
    convert_utf8_to_u32(std::string_view{utf8}, out);
    ASSERT(out.size() == 1);
    ASSERT(out[0] == 0x1F600);
    return true;
}

bool test_utf8_to_u32_empty()
{
    std::u32string out = U"x";
    std::string_view utf8;
    convert_utf8_to_u32(utf8, out);
    ASSERT(out.empty());
    return true;
}

// ═══════════════════════════════════════════════════════
// UTF-32 → UTF-8 (convert_u32_to_utf8)
// ═══════════════════════════════════════════════════════

bool test_u32_to_utf8_ascii()
{
    std::string out;
    std::u32string_view u32s = U"Hi";
    convert_u32_to_utf8(u32s, out);
    ASSERT(out == "Hi");
    return true;
}

bool test_u32_to_utf8_multibyte()
{
    std::string out;
    // U+4E2D (中) → UTF-8: E4 B8 AD
    char32_t cp = 0x4E2D;
    convert_u32_to_utf8(std::u32string_view{&cp, 1}, out);
    ASSERT(out.size() == 3);
    ASSERT(static_cast<uint8_t>(out[0]) == 0xE4);
    ASSERT(static_cast<uint8_t>(out[1]) == 0xB8);
    ASSERT(static_cast<uint8_t>(out[2]) == 0xAD);
    return true;
}

bool test_u32_to_utf8_4byte()
{
    // U+1F600 → UTF-8: F0 9F 98 80
    std::string out;
    char32_t cp = 0x1F600;
    convert_u32_to_utf8(std::u32string_view{&cp, 1}, out);
    ASSERT(out.size() == 4);
    ASSERT(static_cast<uint8_t>(out[0]) == 0xF0);
    return true;
}

// ═══════════════════════════════════════════════════════
// utf8_stream_decoder (逐字节流式解码)
// ═══════════════════════════════════════════════════════

bool test_stream_decoder_ascii()
{
    utf8_stream_decoder dec;
    // 'A' = 0x41，单字节
    auto r = dec(0x41);
    ASSERT(r.has_value());
    ASSERT(*r == U'A');
    return true;
}

bool test_stream_decoder_2byte()
{
    utf8_stream_decoder dec;
    // é = C3 A9
    auto r1 = dec(0xC3);
    ASSERT(!r1.has_value()); // 需要更多字节
    auto r2 = dec(0xA9);
    ASSERT(r2.has_value());
    ASSERT(*r2 == 0x00E9);
    return true;
}

bool test_stream_decoder_3byte()
{
    utf8_stream_decoder dec;
    // 中 = E4 B8 AD
    ASSERT(!dec(0xE4).has_value());
    ASSERT(!dec(0xB8).has_value());
    auto r = dec(0xAD);
    ASSERT(r.has_value());
    ASSERT(*r == 0x4E2D);
    return true;
}

bool test_stream_decoder_invalid_byte()
{
    utf8_stream_decoder dec;
    // 0xFF 是非法的 UTF-8 首字节 → U+FFFD
    auto r = dec(0xFF);
    ASSERT(r.has_value());
    ASSERT(*r == 0xFFFD);
    return true;
}

bool test_stream_decoder_truncated()
{
    utf8_stream_decoder dec;
    // 只给首字节 C3，然后 reset
    ASSERT(!dec(0xC3).has_value());
    dec.reset();
    // reset 后可以正常解码新字符
    auto r = dec('A');
    ASSERT(r.has_value());
    ASSERT(*r == U'A');
    return true;
}

// ═══════════════════════════════════════════════════════
// ANSI → UTF-32 (convert_ansi_to_u32)
// ═══════════════════════════════════════════════════════

bool test_ansi_to_u32_ascii()
{
    std::u32string out;
    std::wstring wbuf;
    // 纯 ASCII 在 CP_ACP 下应保持不变
    convert_ansi_to_u32("ABC", 3, CP_ACP, out, wbuf);
    ASSERT(out.size() == 3);
    ASSERT(out[0] == U'A');
    ASSERT(out[2] == U'C');
    return true;
}

bool test_ansi_to_u32_empty()
{
    std::u32string out = U"x";
    std::wstring wbuf;
    convert_ansi_to_u32("", 0, 0, out, wbuf);
    ASSERT(out.empty());
    return true;
}

bool test_ansi_to_u32_persistent_wbuf()
{
    // 同一 wbuf 两次不同长度的转换
    std::u32string out;
    std::wstring wbuf;
    convert_ansi_to_u32("AB", 2, 0, out, wbuf);
    ASSERT(out.size() == 2);
    convert_ansi_to_u32("CDEF", 4, 0, out, wbuf);
    ASSERT(out.size() == 4);
    ASSERT(out[0] == U'C');
    return true;
}

// ═══════════════════════════════════════════════════════
// 单码点辅助函数
// ═══════════════════════════════════════════════════════

bool test_to_wchar_bmp()
{
    wchar_t out[2]{};
    int n = to_wchar(U'A', out);
    ASSERT(n == 1);
    ASSERT(out[0] == L'A');
    return true;
}

bool test_to_wchar_surrogate()
{
    wchar_t out[2]{};
    int n = to_wchar(0x1F600, out);
    ASSERT(n == 2);
    ASSERT(out[0] == 0xD83D);
    ASSERT(out[1] == 0xDE00);
    return true;
}

bool test_to_char32_surrogate_pair()
{
    const wchar_t src[] = {0xD83D, 0xDE00, 0};
    const wchar_t *it = src;
    char32_t cp = to_char32_surrogate(it, src + 2);
    ASSERT(cp == 0x1F600);
    ASSERT(it == src + 2);
    return true;
}

bool test_to_char32_surrogate_broken()
{
    // 只有高位代理，缺少低位 → U+FFFD
    const wchar_t src[] = {0xD83D, 0};
    const wchar_t *it = src;
    char32_t cp = to_char32_surrogate(it, src + 1);
    ASSERT(cp == 0xFFFD);
    return true;
}

// ═══════════════════════════════════════════════════════
// Test Runner
// ═══════════════════════════════════════════════════════

int main()
{
    std::wcout << L"=== char_convert Tests ===" << std::endl;

    RUN_TEST(test_utf16_to_u32_bmp, L"UTF-16→32 BMP");
    RUN_TEST(test_utf16_to_u32_empty, L"UTF-16→32 Empty");
    RUN_TEST(test_utf16_to_u32_surrogate, L"UTF-16→32 Surrogate");
    RUN_TEST(test_utf16_to_u32_large, L"UTF-16→32 Large");
    RUN_TEST(test_utf16_to_u32_persistent, L"UTF-16→32 Persistent");

    RUN_TEST(test_u32_to_wstr_bmp, L"UTF-32→WSTR BMP");
    RUN_TEST(test_u32_to_wstr_surrogate, L"UTF-32→WSTR Surrogate");
    RUN_TEST(test_u32_to_wstr_empty, L"UTF-32→WSTR Empty");

    RUN_TEST(test_utf8_to_u32_ascii, L"UTF-8→32 ASCII");
    RUN_TEST(test_utf8_to_u32_2byte, L"UTF-8→32 2-byte");
    RUN_TEST(test_utf8_to_u32_3byte, L"UTF-8→32 3-byte");
    RUN_TEST(test_utf8_to_u32_4byte, L"UTF-8→32 4-byte");
    RUN_TEST(test_utf8_to_u32_empty, L"UTF-8→32 Empty");

    RUN_TEST(test_u32_to_utf8_ascii, L"UTF-32→8 ASCII");
    RUN_TEST(test_u32_to_utf8_multibyte, L"UTF-32→8 3-byte");
    RUN_TEST(test_u32_to_utf8_4byte, L"UTF-32→8 4-byte");

    RUN_TEST(test_stream_decoder_ascii, L"Stream ASCII");
    RUN_TEST(test_stream_decoder_2byte, L"Stream 2-byte");
    RUN_TEST(test_stream_decoder_3byte, L"Stream 3-byte");
    RUN_TEST(test_stream_decoder_invalid_byte, L"Stream Invalid");
    RUN_TEST(test_stream_decoder_truncated, L"Stream Truncated");

    RUN_TEST(test_ansi_to_u32_ascii, L"ANSI→32 ASCII");
    RUN_TEST(test_ansi_to_u32_empty, L"ANSI→32 Empty");
    RUN_TEST(test_ansi_to_u32_persistent_wbuf, L"ANSI→32 Persistent");

    RUN_TEST(test_to_wchar_bmp, L"to_wchar BMP");
    RUN_TEST(test_to_wchar_surrogate, L"to_wchar Surrogate");
    RUN_TEST(test_to_char32_surrogate_pair, L"to_char32_surrogate");
    RUN_TEST(test_to_char32_surrogate_broken, L"to_char32 broken surrogate");

    std::wcout << L"  " << tests_passed << L" passed, " << tests_failed << L" failed, " << (tests_passed + tests_failed)
               << L" total." << std::endl;
    return tests_failed > 0 ? 1 : 0;
}
