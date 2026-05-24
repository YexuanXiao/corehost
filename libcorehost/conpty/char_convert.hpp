// ── conpty/char_convert.hpp ─────────────────────────
// 编码转换工具 — 所有函数写入预分配的持久缓冲区
//
// 依赖: libunicode (third_parties/libunicode/src/libunicode/convert.h)
//       Windows MultiByteToWideChar / WideCharToMultiByte (ANSI路径)
// 原则:
//   - ByteSize = WideByteSize = U32ByteSize（保守上界估计）
//   - CP == 65001 (UTF-8) 时走 libunicode SIMD 快速路径
//   - 流式 UTF-8 解码 (utf8_stream_decoder): 逐字节喂入
//
#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
#include <string_view>
#include <array>
#include <optional>
#include <libunicode/convert.h>

namespace conpty
{

// ── 流式 UTF-8 → char32_t 解码器 ─────────────────────
// 纯状态机实现 (零外部依赖)，非法字节返回 U+FFFD
struct utf8_stream_decoder
{
    unsigned _buf[4]{};
    int _len = 0;
    int _expected = 0;

    std::optional<char32_t> operator()(uint8_t byte)
    {
        if (_expected == 0)
        {
            if (byte <= 0x7F)
                return static_cast<char32_t>(byte);
            if (byte >= 0xC2 && byte <= 0xDF)
                _expected = 2;
            else if (byte >= 0xE0 && byte <= 0xEF)
                _expected = 3;
            else if (byte >= 0xF0 && byte <= 0xF4)
                _expected = 4;
            else
                return U'\xFFFD'; // 非法首字节
            _buf[0] = byte;
            _len = 1;
            return std::nullopt;
        }

        _buf[_len++] = byte;
        if (_len == _expected)
        {
            char32_t result = U'\xFFFD';
            if (_expected == 2)
            {
                if ((_buf[1] & 0xC0) == 0x80)
                    result = ((_buf[0] & 0x1F) << 6) | (_buf[1] & 0x3F);
            }
            else if (_expected == 3)
            {
                if ((_buf[1] & 0xC0) == 0x80 && (_buf[2] & 0xC0) == 0x80)
                    result = ((_buf[0] & 0x0F) << 12) | ((_buf[1] & 0x3F) << 6) | (_buf[2] & 0x3F);
            }
            else if (_expected == 4)
            {
                if ((_buf[1] & 0xC0) == 0x80 && (_buf[2] & 0xC0) == 0x80 && (_buf[3] & 0xC0) == 0x80)
                    result = ((_buf[0] & 0x07) << 18) | ((_buf[1] & 0x3F) << 12) | ((_buf[2] & 0x3F) << 6) |
                             (_buf[3] & 0x3F);
            }
            _expected = 0;
            _len = 0;
            return (result <= 0x10FFFF) ? std::optional<char32_t>{result} : std::optional<char32_t>{U'\xFFFD'};
        }
        return std::nullopt;
    }

    void reset() noexcept
    {
        _len = 0;
        _expected = 0;
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
    unicode::encoder<char> enc;
    char *p = buf;
    p = enc(cp, p);
    return static_cast<int>(p - buf);
}

// ── char32_t → wchar_t (UTF-16，可能代理对) ──────────
inline int to_wchar(char32_t cp, wchar_t *out) noexcept
{
    if (cp <= 0xFFFF)
    {
        if (cp >= 0xD800 && cp <= 0xDFFF)
        {
            out[0] = 0xFFFD;
            return 1;
        }
        out[0] = static_cast<wchar_t>(cp);
        return 1;
    }
    cp -= 0x10000;
    out[0] = static_cast<wchar_t>(0xD800 + (cp >> 10));
    out[1] = static_cast<wchar_t>(0xDC00 + (cp & 0x3FF));
    return 2;
}

// ── UTF-16 → char32_t ────────────────────────────────
inline char32_t to_char32(wchar_t ch) noexcept
{
    if (ch >= 0xD800 && ch <= 0xDFFF)
        return 0xFFFD;
    return static_cast<char32_t>(ch);
}

inline char32_t to_char32_surrogate(const wchar_t *&it, const wchar_t *end) noexcept
{
    wchar_t hi = *it;
    if (hi < 0xD800 || hi > 0xDBFF)
    {
        ++it;
        return to_char32(hi);
    }
    ++it;
    if (it == end)
        return 0xFFFD;
    wchar_t lo = *it;
    if (lo < 0xDC00 || lo > 0xDFFF)
        return 0xFFFD;
    ++it;
    return static_cast<char32_t>(0x10000 + ((hi - 0xD800) << 10) + (lo - 0xDC00));
}

// ════════════════════════════════════════════════════════
// 持久化批量转换函数 (输出写入引用参数 out)
// ════════════════════════════════════════════════════════

// UTF-16 → UTF-32
inline void convert_utf16_to_u32(std::wstring_view ws, std::u32string &out)
{
    if (ws.empty())
    {
        out.clear();
        return;
    }
    out = unicode::convert_to<char32_t>(std::u16string_view{reinterpret_cast<const char16_t *>(ws.data()), ws.size()});
}

// UTF-8 → UTF-32（SIMD快速路径）
inline void convert_utf8_to_u32(std::string_view utf8, std::u32string &out)
{
    if (utf8.empty())
    {
        out.clear();
        return;
    }
    out = unicode::convert_to<char32_t>(utf8);
}

// UTF-32 → UTF-16 (wstring)
inline void convert_u32_to_wstr(std::u32string_view u32s, std::wstring &out)
{
    if (u32s.empty())
    {
        out.clear();
        return;
    }
    auto u16 = unicode::convert_to<char16_t>(u32s);
    out.assign(reinterpret_cast<const wchar_t *>(u16.data()), u16.size());
}

// UTF-32 → UTF-8
inline void convert_u32_to_utf8(std::u32string_view u32s, std::string &out)
{
    if (u32s.empty())
    {
        out.clear();
        return;
    }
    out = unicode::convert_to<char>(u32s);
}

// ════════════════════════════════════════════════════════
// UTF-8 ↔ UTF-16
// ════════════════════════════════════════════════════════

// UTF-8 → UTF-16 (wstring)（SIMD快速路径）
inline void convert_utf8_to_wstr(std::string_view u8, std::wstring &out)
{
    if (u8.empty())
    {
        out.clear();
        return;
    }
    auto u16 = unicode::convert_to<char16_t>(u8);
    out.assign(reinterpret_cast<const wchar_t *>(u16.data()), u16.size());
}

// UTF-16 (wstring) → UTF-8
inline void convert_wstr_to_utf8(std::wstring_view ws, std::string &out)
{
    if (ws.empty())
    {
        out.clear();
        return;
    }
    out = unicode::convert_to<char>(std::u16string_view{reinterpret_cast<const char16_t *>(ws.data()), ws.size()});
}

// ════════════════════════════════════════════════════════
// ANSI ↔ UTF-16 / UTF-32 / UTF-8
// 原则: ByteSize = WideByteSize = U32ByteSize（保守上界估计）
//       CP == 65001 (UTF-8) 时走 libunicode SIMD 快速路径
// ════════════════════════════════════════════════════════

// ── 预分配大小估计 ──
inline size_t ansi_to_wide_est(size_t ansi_bytes) noexcept
{
    return ansi_bytes;
}
inline size_t wide_to_ansi_est(size_t wide_bytes) noexcept
{
    return wide_bytes;
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
        convert_utf8_to_wstr(std::string_view{s, len}, out);
        return;
    }
    out.resize_and_overwrite(len, [&](wchar_t *p, size_t cap) -> size_t {
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
    out.resize_and_overwrite(ws.size() * 3, [&](char *p, size_t cap) -> size_t {
        int n = ::WideCharToMultiByte(cp, 0, ws.data(), static_cast<int>(ws.size()), p, static_cast<int>(cap), nullptr,
                                      nullptr);
        return n > 0 ? static_cast<size_t>(n) : 0;
    });
}

// wstring → ANSI 长度查询
inline size_t wstr_to_ansi_len(std::wstring_view ws, UINT cp) noexcept
{
    if (ws.empty())
        return 0;
    if (cp == CP_UTF8 || cp == 65001)
        return ws.size() * 3; // 保守上界: UTF-8 最多 4字节/BMP字符
    int n = ::WideCharToMultiByte(cp, 0, ws.data(), static_cast<int>(ws.size()), nullptr, 0, nullptr, nullptr);
    return n > 0 ? static_cast<size_t>(n) : 0;
}

// ANSI → wstring 长度查询
inline size_t ansi_to_wstr_len(const char *s, size_t len, UINT cp) noexcept
{
    if (len == 0)
        return 0;
    if (cp == CP_UTF8 || cp == 65001)
        return len; // UTF-8字节数 ≥ UTF-16码元数
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
    convert_ansi_to_wstr(s, len, cp, wbuf);
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
        auto *p = reinterpret_cast<char16_t *>(out);
        size_t n = unicode::detail::convert_utf8_to_utf16(s, len, p);
        return n <= out_cap ? n : 0;
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
        // UTF-8 保守上界: 每个 u32_codepoint ≤ 4 字节
        auto u8 = unicode::convert_to<char>(std::u16string_view{reinterpret_cast<const char16_t *>(s), len});
        if (u8.size() < out_cap)
        {
            std::memcpy(out, u8.data(), u8.size());
            out[u8.size()] = '\0';
            return u8.size();
        }
        return 0;
    }
    int n = ::WideCharToMultiByte(cp, 0, s, static_cast<int>(len), out, static_cast<int>(out_cap), nullptr, nullptr);
    if (n > 0)
    {
        out[n] = '\0';
        return static_cast<size_t>(n);
    }
    return 0;
}

} // namespace conpty
