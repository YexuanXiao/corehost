#pragma once
#include <windows.h>
#include <cassert>
#include <charconv>
#include <span>
#include <string_view>
#include <vector>
#include "win32/handle.hpp"
#include "char_convert.hpp"
#include "perf_diag.hpp"
#include "utility/raw_byte_allocator.hpp"

namespace conpty
{

class vt_output_buffer
{
  public:
    // 预留一个 flush threshold 大小，避免常见批量输出第一次 append 就分配。
    vt_output_buffer()
    {
        _buffer.reserve(flush_threshold);
    }

    // 绑定终端输出 pipe；本类不拥有句柄，只在 flush 时写入。
    void set_output(win32::handle_view output) noexcept
    {
        // output 是终端 VT 输出 pipe；本类只保存 view，不拥有句柄。
        _output = output;
    }

    // 返回当前已经缓存但尚未写入 vt_out 的字节数。
    size_t buffered_size() const noexcept
    {
        return _buffer.size();
    }

    // 判断缓存是否达到主动刷新阈值。
    bool should_flush() const noexcept
    {
        // true 表示缓冲已经足够大，I/O loop 可以在当前 completion 边界后
        // 主动 WriteFile，减少终端可见延迟。
        return _buffer.size() >= flush_threshold;
    }

    // 把缓存内容一次性写入 vt_out；写入后无论 WriteFile 是否完整成功都清空缓存。
    void flush()
    {
        // flush 是唯一真正写 vt_out 的位置；调用方负责选择 completion 前后
        // 的刷新时机，本类不理解 ConDrv 请求边界。
        COREHOST_PERF_SCOPE_AMOUNT(vt_output_flush, _buffer.size());
        if (_buffer.empty())
            return;
        DWORD written = 0;
        {
            COREHOST_PERF_SCOPE_AMOUNT(vt_output_write_file, _buffer.size());
            ::WriteFile(_output.get(), _buffer.data(), static_cast<DWORD>(_buffer.size()), &written, nullptr);
        }
        _buffer.clear();
    }

    // 追加已经是 UTF-8/VT 的字节序列。
    void append(std::string_view text)
    {
        // text 已经是目标终端需要的 UTF-8/ASCII VT 字节，不再转码。
        const auto bytes = std::span{reinterpret_cast<const char8_t *>(text.data()), text.size()};
        _buffer.append_range(bytes);
    }

    // 追加 UTF-32 文本并直接转码到最终输出缓冲。
    void append_utf32(std::u32string_view text)
    {
        // text 来自 parser/screenbuffer 的 char32_t 语义层；直接转入最终
        // vt_out 缓冲，避免先生成临时 UTF-8 字符串。
        if (text.empty())
            return;

        const auto offset = _buffer.size();
        _buffer.resize(offset + text.size() * 4);
        auto *first = reinterpret_cast<char *>(_buffer.data() + offset);
        auto *end = unicode::convert_to<char>(text, first);
        _buffer.resize(offset + static_cast<size_t>(end - first));
    }

    // 追加 UTF-16 文本并直接转码到最终输出缓冲。
    void append_utf16(std::wstring_view text)
    {
        // text 来自 Win32/WT 的 UTF-16 边界，如窗口标题或 WriteConsoleW 快路径。
        if (text.empty())
            return;

        const auto offset = _buffer.size();
        _buffer.resize(offset + text.size() * 3);
        auto *first = reinterpret_cast<char *>(_buffer.data() + offset);
        auto *end = unicode::convert_to<char>(
            std::u16string_view{reinterpret_cast<const char16_t *>(text.data()), text.size()}, first);
        _buffer.resize(offset + static_cast<size_t>(end - first));
    }

#ifdef COREHOST_ANSI_OPT
    // 追加 GBK/CP936 文本并直接转码到最终输出缓冲。
    void append_gbk(std::string_view text)
    {
        // text 是 CP936/GBK 字节流；用于非 Unicode Console API 的中文输出路径。
        if (text.empty())
            return;

        const auto offset = _buffer.size();
        _buffer.resize(offset + text.size() * 3);
        auto *first = reinterpret_cast<char *>(_buffer.data() + offset);
        auto *out = first;
        size_t input = 0;
        unicode::encoder<char> enc;
        while (input < text.size())
            out = enc(gbk_decode_next(text.data(), text.size(), input), out);
        _buffer.resize(offset + static_cast<size_t>(out - first));
    }
#endif

    // 追加单个 ASCII/VT 字节。
    void append(char ch)
    {
        _buffer.push_back(static_cast<char8_t>(static_cast<unsigned char>(ch)));
    }

    // 追加 VT 参数整数，直接写入输出缓冲末尾。
    void append_int(int value)
    {
        // value 用于构造 VT 参数，如 CUP row/col 或 SGR 数值。
        constexpr size_t max_int_chars = 16;
        const auto offset = _buffer.size();
        _buffer.resize(offset + max_int_chars);
        auto *first = reinterpret_cast<char *>(_buffer.data() + offset);
        const auto [end, ec] = std::to_chars(first, first + max_int_chars, value);
        assert(ec == std::errc{});
        _buffer.resize(offset + static_cast<size_t>(end - first));
    }

    // 追加单个 Unicode codepoint 的 UTF-8 字节。
    void append_cell(char32_t ch)
    {
        char bytes[8];
        const auto length = to_utf8_bytes(ch, bytes);
        append(std::string_view{bytes, static_cast<size_t>(length)});
    }

  private:
    // 达到该字节数后 should_flush() 提示 I/O loop 主动刷新；小于该值时允许
    // 多个 Console API 输出合并到一次 WriteFile。
    static constexpr size_t flush_threshold = 64 * 1024;

    // vt_out 的非拥有引用；run_conpty_session 保证其生命周期覆盖本缓冲。
    win32::handle_view _output;
    // 等待写入终端的 UTF-8/VT 原始字节，不需要 NUL 结尾。
    std::vector<char8_t, raw_byte_allocator<char8_t>> _buffer;
};

} // namespace conpty
