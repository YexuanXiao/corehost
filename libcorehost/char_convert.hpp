// ── conpty/char_convert.hpp ─────────────────────────
// 编码转换工具。
//
// 功能分解：
// 1. utf8_stream_decoder 逐字节解码 vt_in 输入，未完成序列返回 nullopt。
// 2. UTF-8/UTF-16/UTF-32 批量转换写入调用方提供的持久缓冲，避免热路径分配。
// 3. UTF-8 和 GBK/CP936 有专用快速路径；其他 ANSI 代码页通过 Windows
//    MultiByteToWideChar/WideCharToMultiByte。
#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
#include <string_view>
#include <algorithm>
#include <array>
#include <optional>
#include <libunicode/convert.h>
#include "gbk_table.hpp"

namespace conpty
{

// ── 流式 UTF-8 → char32_t 解码器 ─────────────────────
// 委托 libunicode::decoder<char>，非法字节返回 U+FFFD
struct utf8_stream_decoder
{
    unicode::decoder<char> _dec;

    std::optional<char32_t> operator()(uint8_t byte)
    {
        auto r = _dec(byte);
        if (r.has_value())
            return r;
        // nullopt 有两种含义：多字节序列尚未收齐，或当前字节已经让 decoder
        // 判定非法。expectedLength==0 表示非法状态已复位，返回替换字符。
        if (_dec.expectedLength == 0)
            return U'\xFFFD';
        return std::nullopt;
    }
};

// ── char32_t → UTF-8 单码点编码 ──────────────────────
template <typename OutputIterator>
OutputIterator to_utf8(char32_t cp, OutputIterator out)
{
    unicode::encoder<char> enc;
    return enc(cp, out);
}

inline int to_utf8_bytes(char32_t cp, char (&buf)[8]) noexcept
{
    // buf[8] 足够容纳一个 UTF-8 codepoint；返回值是实际写入字节数，不含 NUL。
    unicode::encoder<char> enc;
    char *p = buf;
    p = enc(cp, p);
    return static_cast<int>(p - buf);
}

// ── char32_t → wchar_t (UTF-16，可能代理对) ──────────
inline int to_wchar(char32_t cp, wchar_t *out) noexcept
{
    unicode::encoder<wchar_t> enc;
    return static_cast<int>(enc(cp, out) - out);
}

// ── 持久批量转换 (写入预分配引用, resize_and_overwrite 零分配) ──
// 上界估计: 1char = 1wchar = 1char32 (UTF-8→/UTF-16→: 1input≤1output)
//           *2 (UTF-32→UTF-16 代理对), *3 (UTF-16→UTF-8), *4 (UTF-32→UTF-8)
// ════════════════════════════════════════════════════════

inline void convert_utf16_to_u32(std::wstring_view ws, std::u32string &out)
{
    if (ws.empty())
    {
        out.clear();
        return;
    }
    out.resize_and_overwrite(ws.size(), [&](char32_t *p, size_t) -> size_t {
        // UTF-16 到 UTF-32 的输出 codepoint 数不会超过输入 code unit 数。
        auto end = unicode::convert_to<char32_t>(
            std::u16string_view{reinterpret_cast<const char16_t *>(ws.data()), ws.size()}, p);
        return static_cast<size_t>(end - p);
    });
}

inline void convert_utf8_to_u32(std::string_view utf8, std::u32string &out)
{
    if (utf8.empty())
    {
        out.clear();
        return;
    }
    out.resize_and_overwrite(utf8.size(), [&](char32_t *p, size_t) -> size_t {
        return unicode::detail::convert_utf8_to_utf32(utf8.data(), utf8.size(), p);
    });
}

inline void convert_u32_to_wstr(std::u32string_view u32s, std::wstring &out)
{
    if (u32s.empty())
    {
        out.clear();
        return;
    }
    out.resize_and_overwrite(u32s.size() * 2, [&](wchar_t *p, size_t) -> size_t {
        // Windows wchar_t 是 UTF-16，非 BMP codepoint 最多展开成两个 code unit。
        auto end = unicode::convert_to<char16_t>(u32s, reinterpret_cast<char16_t *>(p));
        return static_cast<size_t>(end - reinterpret_cast<char16_t *>(p));
    });
}

inline void convert_u32_to_utf8(std::u32string_view u32s, std::string &out)
{
    if (u32s.empty())
    {
        out.clear();
        return;
    }
    out.resize_and_overwrite(u32s.size() * 4, [&](char *p, size_t) -> size_t {
        auto end = unicode::convert_to<char>(u32s, p);
        return static_cast<size_t>(end - p);
    });
}

inline size_t utf16_prefix_units(std::wstring_view text, size_t max_units) noexcept
{
    auto count = std::min(text.size(), max_units);
    if constexpr (sizeof(wchar_t) == 2)
    {
        if (count > 0 && count < text.size())
        {
            const auto last = static_cast<unsigned>(text[count - 1]);
            const auto next = static_cast<unsigned>(text[count]);
            if (last >= 0xD800 && last <= 0xDBFF && next >= 0xDC00 && next <= 0xDFFF)
                --count;
        }
    }
    return count;
}

inline void convert_utf8_to_wstr(std::string_view u8, std::wstring &out)
{
    if (u8.empty())
    {
        out.clear();
        return;
    }
    out.resize_and_overwrite(u8.size(), [&](wchar_t *p, size_t) -> size_t {
        if constexpr (sizeof(wchar_t) == 2)
            return unicode::detail::convert_utf8_to_utf16(u8.data(), u8.size(), reinterpret_cast<char16_t *>(p));
        else
            return unicode::detail::convert_utf8_to_utf32(u8.data(), u8.size(), reinterpret_cast<char32_t *>(p));
    });
}

inline void convert_wstr_to_utf8(std::wstring_view ws, std::string &out)
{
    if (ws.empty())
    {
        out.clear();
        return;
    }
    out.resize_and_overwrite(ws.size() * 3, [&](char *p, size_t) -> size_t {
        auto end =
            unicode::convert_to<char>(std::u16string_view{reinterpret_cast<const char16_t *>(ws.data()), ws.size()}, p);
        return static_cast<size_t>(end - p);
    });
}

// ════════════════════════════════════════════════════════
// ANSI ↔ UTF-16 / UTF-32 / UTF-8
// 原则: ByteSize = WideByteSize = U32ByteSize（保守上界估计）
//       CP == 65001 (UTF-8) 时走 libunicode SIMD 快速路径
//       CP == 936 (GBK) 时走本地 GBK↔UTF-32 表
// ════════════════════════════════════════════════════════

inline constexpr UINT code_page_gbk = 936;
inline constexpr char32_t unicode_replacement_character = U'\xFFFD';
inline constexpr char gbk_default_byte = '?';
inline constexpr size_t gbk_lead_first = 0x81;
inline constexpr size_t gbk_lead_last = 0xFE;
inline constexpr size_t gbk_trail_first = 0x40;
inline constexpr size_t gbk_trail_last = 0xFE;

constexpr bool gbk_is_lead(uint8_t byte) noexcept
{
    return byte >= gbk_lead_first && byte <= gbk_lead_last;
}

constexpr bool gbk_is_trail(uint8_t byte) noexcept
{
    return byte >= gbk_trail_first && byte <= gbk_trail_last && byte != 0x7F;
}

template <size_t N>
inline const gbk_range *gbk_find_range(const std::array<gbk_range, N> &ranges, uint32_t key) noexcept
{
    size_t first = 0;
    size_t last = ranges.size();
    while (first < last)
    {
        const auto mid = first + (last - first) / 2;
        const auto &range = ranges[mid];
        if (key < range.first)
            last = mid;
        else if (key > range.last)
            first = mid + 1;
        else
            return &range;
    }
    return nullptr;
}

inline char32_t gbk_decode_code(uint16_t code) noexcept
{
    const auto *range = gbk_find_range(gbk_decode_ranges, code);
    if (!range)
        return gbk_invalid_codepoint;
    return static_cast<char32_t>(range->mapped_first + (code - range->first));
}

inline char32_t gbk_decode_next(const char *s, size_t len, size_t &i) noexcept
{
    const auto byte = static_cast<uint8_t>(s[i]);
    if (byte <= 0x7F)
    {
        ++i;
        return static_cast<char32_t>(byte);
    }
    if (byte == 0x80)
    {
        ++i;
        return U'\x20AC';
    }

    if (!gbk_is_lead(byte) || i + 1 >= len)
    {
        ++i;
        return unicode_replacement_character;
    }

    const auto trail = static_cast<uint8_t>(s[i + 1]);
    if (!gbk_is_trail(trail))
    {
        ++i;
        return unicode_replacement_character;
    }

    const auto code = static_cast<uint16_t>((byte << 8) | trail);
    const auto mapped = gbk_decode_code(code);
    i += 2;
    return mapped == gbk_invalid_codepoint ? unicode_replacement_character : mapped;
}

inline uint16_t gbk_encode_codepoint(char32_t cp) noexcept
{
    if (cp <= 0x7F)
        return static_cast<uint16_t>(cp);
    if (cp == U'\x20AC')
        return 0x80;
    if (cp > 0xFFFF)
        return static_cast<uint8_t>(gbk_default_byte);
    const auto *range = gbk_find_range(gbk_encode_ranges, static_cast<uint32_t>(cp));
    if (!range)
        return static_cast<uint8_t>(gbk_default_byte);
    return static_cast<uint16_t>(range->mapped_first + (static_cast<uint32_t>(cp) - range->first));
}

inline void gbk_append_code(uint16_t code, std::string &out, size_t &written) noexcept
{
    if (code <= 0xFF)
    {
        out[written++] = static_cast<char>(code);
        return;
    }
    out[written++] = static_cast<char>(code >> 8);
    out[written++] = static_cast<char>(code & 0xFF);
}

inline bool gbk_append_code_raw(uint16_t code, char *out, size_t out_cap, size_t &written) noexcept
{
    if (code <= 0xFF)
    {
        if (written + 1 > out_cap)
            return false;
        out[written++] = static_cast<char>(code);
        return true;
    }
    if (written + 2 > out_cap)
        return false;
    out[written++] = static_cast<char>(code >> 8);
    out[written++] = static_cast<char>(code & 0xFF);
    return true;
}

inline void convert_gbk_to_u32(const char *s, size_t len, std::u32string &out)
{
    out.resize(len);
    size_t i = 0;
    size_t written = 0;
    while (i < len)
        out[written++] = gbk_decode_next(s, len, i);
    out.resize(written);
}

inline void convert_gbk_to_wstr(const char *s, size_t len, std::wstring &out)
{
    out.resize(len);
    size_t i = 0;
    size_t written = 0;
    while (i < len)
        out[written++] = static_cast<wchar_t>(gbk_decode_next(s, len, i));
    out.resize(written);
}

inline void convert_gbk_to_utf8(const char *s, size_t len, std::string &out)
{
    out.resize(len * 3);
    unicode::encoder<char> enc;
    char *p = out.data();
    size_t i = 0;
    while (i < len)
        p = enc(gbk_decode_next(s, len, i), p);
    out.resize(static_cast<size_t>(p - out.data()));
}

inline bool convert_gbk_to_wide_raw(const char *s, size_t len, wchar_t *out, size_t out_cap, size_t &written) noexcept
{
    size_t i = 0;
    written = 0;
    while (i < len)
    {
        if (written >= out_cap)
            return false;
        out[written++] = static_cast<wchar_t>(gbk_decode_next(s, len, i));
    }
    return true;
}

inline void convert_u32_to_gbk(std::u32string_view u32s, std::string &out)
{
    out.resize(u32s.size() * 2);
    size_t written = 0;
    for (char32_t cp : u32s)
        gbk_append_code(gbk_encode_codepoint(cp), out, written);
    out.resize(written);
}

inline void convert_wstr_to_gbk(std::wstring_view ws, std::string &out)
{
    out.resize(ws.size() * 2);
    size_t written = 0;
    for (size_t i = 0; i < ws.size(); ++i)
    {
        const auto wc = static_cast<char32_t>(ws[i]);
        if (wc >= 0xD800 && wc <= 0xDBFF && i + 1 < ws.size())
        {
            const auto next = static_cast<char32_t>(ws[i + 1]);
            if (next >= 0xDC00 && next <= 0xDFFF)
            {
                gbk_append_code(static_cast<uint8_t>(gbk_default_byte), out, written);
                ++i;
                continue;
            }
        }
        gbk_append_code(gbk_encode_codepoint(wc), out, written);
    }
    out.resize(written);
}

inline bool convert_wide_to_gbk_raw(const wchar_t *s, size_t len, char *out, size_t out_cap, size_t &written) noexcept
{
    written = 0;
    for (size_t i = 0; i < len; ++i)
    {
        const auto wc = static_cast<char32_t>(s[i]);
        if (wc >= 0xD800 && wc <= 0xDBFF && i + 1 < len)
        {
            const auto next = static_cast<char32_t>(s[i + 1]);
            if (next >= 0xDC00 && next <= 0xDFFF)
            {
                if (!gbk_append_code_raw(static_cast<uint8_t>(gbk_default_byte), out, out_cap, written))
                    return false;
                ++i;
                continue;
            }
        }
        if (!gbk_append_code_raw(gbk_encode_codepoint(wc), out, out_cap, written))
            return false;
    }
    return true;
}

inline size_t gbk_to_wstr_len(const char *s, size_t len) noexcept
{
    size_t i = 0;
    size_t count = 0;
    while (i < len)
    {
        (void)gbk_decode_next(s, len, i);
        ++count;
    }
    return count;
}

inline size_t wstr_to_gbk_len(std::wstring_view ws) noexcept
{
    size_t bytes = 0;
    for (size_t i = 0; i < ws.size(); ++i)
    {
        const auto wc = static_cast<char32_t>(ws[i]);
        if (wc >= 0xD800 && wc <= 0xDBFF && i + 1 < ws.size())
        {
            const auto next = static_cast<char32_t>(ws[i + 1]);
            if (next >= 0xDC00 && next <= 0xDFFF)
            {
                ++bytes;
                ++i;
                continue;
            }
        }
        bytes += gbk_encode_codepoint(wc) <= 0xFF ? 1 : 2;
    }
    return bytes;
}
// ── 上界估计: 1char→1wchar， 1wchar→2char(ANSI) 或 3char(UTF-8) ──
inline size_t ansi_to_wide_est(size_t ansi_bytes) noexcept
{
    return ansi_bytes;
}
inline size_t wide_to_ansi_est(size_t wlen, UINT cp) noexcept
{
    return (cp == CP_UTF8 || cp == 65001) ? wlen * 3 : wlen * 2;
}

// ANSI → wstring
inline void convert_ansi_to_wstr(const char *s, size_t len, UINT cp, std::wstring &out)
{
    if (len == 0)
    {
        out.clear();
        return;
    }
    if (cp == CP_UTF8 || cp == 65001)
    {
        // CP_UTF8 走 libunicode，避免 Windows API 对非法 UTF-8 的不同容错策略
        // 影响 VT 输入/输出路径。
        convert_utf8_to_wstr(std::string_view{s, len}, out);
        return;
    }
    if (cp == code_page_gbk)
    {
        convert_gbk_to_wstr(s, len, out);
        return;
    }
    out.resize_and_overwrite(ansi_to_wide_est(len), [&](wchar_t *p, size_t cap) -> size_t {
        int wl = ::MultiByteToWideChar(cp, 0, s, static_cast<int>(len), p, static_cast<int>(cap));
        return wl > 0 ? static_cast<size_t>(wl) : 0;
    });
}

// wstring → ANSI
inline void convert_wstr_to_ansi(std::wstring_view ws, UINT cp, std::string &out)
{
    if (ws.empty())
    {
        out.clear();
        return;
    }
    if (cp == CP_UTF8 || cp == 65001)
    {
        convert_wstr_to_utf8(ws, out);
        return;
    }
    if (cp == code_page_gbk)
    {
        convert_wstr_to_gbk(ws, out);
        return;
    }
    out.resize_and_overwrite(wide_to_ansi_est(ws.size(), cp), [&](char *p, size_t cap) -> size_t {
        int n = ::WideCharToMultiByte(cp, 0, ws.data(), static_cast<int>(ws.size()), p, static_cast<int>(cap), nullptr,
                                      nullptr);
        return n > 0 ? static_cast<size_t>(n) : 0;
    });
}

inline void convert_ansi_to_utf8(const char *s, size_t len, UINT cp, std::string &out, std::wstring &wbuf)
{
    if (len == 0)
    {
        out.clear();
        return;
    }
    if (cp == CP_UTF8 || cp == 65001)
    {
        out.assign(s, len);
        return;
    }
    if (cp == code_page_gbk)
    {
        convert_gbk_to_utf8(s, len, out);
        return;
    }
    convert_ansi_to_wstr(s, len, cp, wbuf);
    convert_wstr_to_utf8(std::wstring_view{wbuf.data(), wbuf.size()}, out);
}

// wstring → ANSI 字节数。非 UTF-8 ANSI 走 Windows 代码页长度查询；
// UTF-8 分支禁止用 Win32 UTF 转换，只返回编码上界。
inline size_t wstr_to_ansi_len(std::wstring_view ws, UINT cp) noexcept
{
    if (ws.empty())
        return 0;
    if (cp == CP_UTF8 || cp == 65001)
        return wide_to_ansi_est(ws.size(), cp);
    if (cp == code_page_gbk)
        return wstr_to_gbk_len(ws);
    int n = ::WideCharToMultiByte(cp, 0, ws.data(), static_cast<int>(ws.size()), nullptr, 0, nullptr, nullptr);
    return n > 0 ? static_cast<size_t>(n) : 0;
}

// ANSI → wstring 字符数。非 UTF-8 ANSI 走 Windows 代码页长度查询；
// UTF-8 分支只返回编码上界。
inline size_t ansi_to_wstr_len(const char *s, size_t len, UINT cp) noexcept
{
    if (len == 0)
        return 0;
    if (cp == CP_UTF8 || cp == 65001)
        return ansi_to_wide_est(len);
    if (cp == code_page_gbk)
        return gbk_to_wstr_len(s, len);
    int wl = ::MultiByteToWideChar(cp, 0, s, static_cast<int>(len), nullptr, 0);
    return wl > 0 ? static_cast<size_t>(wl) : 0;
}

// ANSI → UTF-32: CP_UTF8走libunicode SIMD路径, 否则 MultiByteToWideChar + UTF-16→UTF-32
// wbuf: 调用方提供的持久 wchar_t 中间缓冲
inline void convert_ansi_to_u32(const char *s, size_t len, UINT code_page, std::u32string &out, std::wstring &wbuf)
{
    if (len == 0)
    {
        out.clear();
        return;
    }
    UINT cp = code_page ? code_page : CP_ACP;
    if (cp == CP_UTF8 || cp == 65001)
    {
        convert_utf8_to_u32(std::string_view{s, len}, out);
        return;
    }
    if (cp == code_page_gbk)
    {
        convert_gbk_to_u32(s, len, out);
        return;
    }
    convert_ansi_to_wstr(s, len, cp, wbuf);
    // wbuf 是调用方持久缓冲；这里不保留 view，转换完成后可立即复用。
    convert_utf16_to_u32(std::wstring_view{wbuf.data(), wbuf.size()}, out);
}

// UTF-32 → ANSI
// wbuf: 调用方提供的持久 wchar_t 中间缓冲
inline void convert_u32_to_ansi(std::u32string_view u32s, UINT cp, std::string &out, std::wstring &wbuf)
{
    if (u32s.empty())
    {
        out.clear();
        return;
    }
    if (cp == CP_UTF8 || cp == 65001)
    {
        convert_u32_to_utf8(u32s, out);
        return;
    }
    if (cp == code_page_gbk)
    {
        convert_u32_to_gbk(u32s, out);
        return;
    }
    convert_u32_to_wstr(u32s, wbuf);
    convert_wstr_to_ansi(std::wstring_view{wbuf.data(), wbuf.size()}, cp, out);
}

// ════════════════════════════════════════════════════════
// 原始缓冲区写入（用于 ConDrv 消息体直接写入）
// 返回值: 实际写入的元素数 (wchar_t 数或 char 字节数), 0=失败
// ════════════════════════════════════════════════════════

// ANSI → wchar_t* 原始缓冲
inline size_t convert_ansi_to_wide_raw(const char *s, size_t len, UINT cp, wchar_t *out, size_t out_cap)
{
    if (len == 0)
        return 0;
    if (cp == CP_UTF8 || cp == 65001)
    {
        if (len > out_cap)
            return 0;
        return unicode::detail::convert_utf8_to_utf16(s, len, reinterpret_cast<char16_t *>(out));
    }
    if (cp == code_page_gbk)
    {
        size_t written = 0;
        return convert_gbk_to_wide_raw(s, len, out, out_cap, written) ? written : 0;
    }
    int wl = ::MultiByteToWideChar(cp, 0, s, static_cast<int>(len), out, static_cast<int>(out_cap));
    return wl > 0 ? static_cast<size_t>(wl) : 0;
}

// wstring → char* 原始缓冲（含 '\0' 终止）
inline size_t convert_wide_to_ansi_raw(const wchar_t *s, size_t len, UINT cp, char *out, size_t out_cap)
{
    if (len == 0)
    {
        if (out_cap > 0)
            out[0] = '\0';
        return 0;
    }
    if (cp == CP_UTF8 || cp == 65001)
    {
        if (out_cap == 0 || wide_to_ansi_est(len, cp) >= out_cap)
            return 0;
        auto *end = unicode::convert_to<char>(std::u16string_view{reinterpret_cast<const char16_t *>(s), len}, out);
        const auto written = static_cast<size_t>(end - out);
        out[written] = '\0';
        return written;
    }
    if (out_cap == 0)
        return 0;
    if (cp == code_page_gbk)
    {
        size_t written = 0;
        if (convert_wide_to_gbk_raw(s, len, out, out_cap - 1, written))
        {
            out[written] = '\0';
            return written;
        }
        return 0;
    }
    int n =
        ::WideCharToMultiByte(cp, 0, s, static_cast<int>(len), out, static_cast<int>(out_cap - 1), nullptr, nullptr);
    if (n > 0)
    {
        out[n] = '\0';
        return static_cast<size_t>(n);
    }
    return 0;
}

} // namespace conpty

