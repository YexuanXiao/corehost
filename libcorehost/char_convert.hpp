// ── conpty/char_convert.hpp ─────────────────────────
// 编码转换工具。
//
// 功能分解：
// 1. utf8_stream_decoder 逐字节解码 vt_in 输入，未完成序列返回 nullopt。
// 2. UTF-8/UTF-16/UTF-32 批量转换写入调用方提供的持久缓冲，避免热路径分配。
// 3. 非 UTF-8 代码页通过 Windows MultiByteToWideChar/WideCharToMultiByte。
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
// ════════════════════════════════════════════════════════

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
    out.resize_and_overwrite(wide_to_ansi_est(ws.size(), cp), [&](char *p, size_t cap) -> size_t {
        int n = ::WideCharToMultiByte(cp, 0, ws.data(), static_cast<int>(ws.size()), p, static_cast<int>(cap), nullptr,
                                      nullptr);
        return n > 0 ? static_cast<size_t>(n) : 0;
    });
}

// wstring → ANSI 长度查询 (ConDrv 客户端需要精确值)
inline size_t wstr_to_ansi_len(std::wstring_view ws, UINT cp) noexcept
{
    if (ws.empty())
        return 0;
    // 长度查询不包含结尾 NUL，调用者需要自己为 NUL 预留空间。
    int n = ::WideCharToMultiByte(cp, 0, ws.data(), static_cast<int>(ws.size()), nullptr, 0, nullptr, nullptr);
    return n > 0 ? static_cast<size_t>(n) : 0;
}

// ANSI → wstring 长度查询 (ConDrv 客户端需要精确值)
inline size_t ansi_to_wstr_len(const char *s, size_t len, UINT cp) noexcept
{
    if (len == 0)
        return 0;
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
        // raw 写入函数负责补 NUL；如果空间不足以容纳 NUL，返回 0 表示失败。
        auto *end = unicode::convert_to<char>(std::u16string_view{reinterpret_cast<const char16_t *>(s), len}, out);
        size_t n = static_cast<size_t>(end - out);
        if (n < out_cap)
        {
            out[n] = '\0';
            return n;
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
