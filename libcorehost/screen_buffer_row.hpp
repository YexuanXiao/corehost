// ── conpty/screen_buffer_row.hpp ───────────────────
// 屏幕缓冲区单行存储。
//
// 功能分解：
// 1. _text 连续保存一行内所有 glyph 的 char32_t 码点。
// 2. _columns 把屏幕列映射到 _text 偏移；高位标记双宽 glyph 的 trailing 列。
// 3. _attrs 按列保存 Win32 传统属性，CHAR_INFO 导出时直接使用。
//
// grapheme cluster 支持:
//   一个 glyph (grapheme cluster) 可能包含多个 char32_t (如 emoji ZWJ 序列),
//   占据 1 或 2 列。_columns[col] 指向该 glyph 在 _text 中的起始偏移，
//   下一非 trailing 列的偏移差 = glyph 的 char32_t 长度。
//
//   列布局示例 (单宽 "abc" + 双宽 "中" + 单宽 "d"):
//     _text    = [a,b,c,中,d]
//     _columns = [0,1,2,3,3|0x8000,4]  (最后一列为 past-the-end)
//     第 0-2 列: 偏移 0,1,2
//     第 3 列:   偏移 3 (leading half of 中)
//     第 4 列:   偏移 3|0x8000 (trailing half of 中)
//
// CHAR_INFO 仅在 API 边界 (read_rect/write_rect) 转换。
#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include <string_view>
#include <algorithm>
#include <cassert>
#include "perf_diag.hpp"

namespace conpty
{

// ── TextAttribute (对标原始 TextAttribute, 简化版) ──
struct text_attribute
{
    // 0x07 是传统白前景/黑背景；legacy 保存 Win32 16 色属性位。
    WORD legacy = 0x07;

    text_attribute() = default;
    explicit text_attribute(WORD attr) : legacy(attr)
    {
    }
    operator WORD() const noexcept
    {
        return legacy;
    }

    bool operator==(const text_attribute &o) const noexcept
    {
        return legacy == o.legacy;
    }
    bool operator!=(const text_attribute &o) const noexcept
    {
        return legacy != o.legacy;
    }
};

// ── screen_buffer_row ─────────────────────────────────
struct screen_buffer_row
{
    // _columns 的高位标记当前列是双宽/多列 glyph 的 trailing 列；
    // 低 15 位保存 _text 偏移。单行文本长度必须保持在 OFFSET_MASK 范围内。
    static constexpr uint16_t TRAILING_FLAG = 0x8000;
    static constexpr uint16_t OFFSET_MASK = 0x7FFF;

    // _text 连续保存所有 glyph 的 char32_t 码点；不包含按列填充的 NUL。
    std::u32string _text;

    // 大小为 width+1。_columns[width] 是 past-the-end 偏移，便于计算最后
    // 一个 glyph 的长度。
    std::vector<uint16_t> _columns;

    // 大小为 width。trailing 列也保存属性，便于 CHAR_INFO 导出。
    std::vector<text_attribute> _attrs;

    screen_buffer_row() = default;

    // ── 构造: 填充 width 列 (默认空格, 默认属性) ──
    explicit screen_buffer_row(uint16_t width, WORD default_attr = 0x07)
    {
        reset_fill(width, U' ', text_attribute{default_attr});
    }

    void reset_fill(uint16_t width, char32_t cp, text_attribute attr)
    {
        _columns.resize(static_cast<size_t>(width) + 1);
        _attrs.resize(width);
        std::fill(_attrs.begin(), _attrs.end(), attr);
        _text.assign(static_cast<size_t>(width), cp);
        for (uint16_t i = 0; i <= width; ++i)
            _columns[i] = i;
    }

    // ── 基本信息 ──
    uint16_t width() const noexcept
    {
        return static_cast<uint16_t>(_columns.empty() ? 0 : _columns.size() - 1);
    }

    // ── 偏移量读取 ──
    uint16_t col_offset(uint16_t col) const noexcept
    {
        // 返回值已经去掉 trailing 标志；越界列按 0 处理，调用者通常会先用
        // width() 做边界检查。
        return (col < _columns.size()) ? (_columns[col] & OFFSET_MASK) : 0;
    }

    bool is_trailing(uint16_t col) const noexcept
    {
        return (col < _columns.size()) && (_columns[col] & TRAILING_FLAG);
    }

    // ── 字素簇 (glyph/grapheme cluster) 读取 ──
    std::u32string_view glyph_at(uint16_t col) const noexcept
    {
        if (col >= width())
            return {};
        uint16_t start = col_offset(col);
        // trailing 列与 leading 列共享 start。end_col 找到下一个非 trailing
        // 列或 past-the-end sentinel，以便得到完整 glyph 的 codepoint 范围。
        uint16_t end_col = col + 1;
        while (end_col < width() && is_trailing(end_col))
            ++end_col;
        uint16_t end = col_offset(end_col);
        if (end > _text.size())
            end = static_cast<uint16_t>(_text.size());
        if (start >= end)
            return {};
        return std::u32string_view{_text.data() + start, static_cast<size_t>(end - start)};
    }

    // 字素簇的列宽
    int glyph_width(uint16_t col) const noexcept
    {
        if (col >= width())
            return 0;
        if (is_trailing(col))
            return 0; // trailing half, width belongs to leading
        // 连续 trailing 列共享 leading 列偏移，计数即 glyph 宽度。
        uint16_t offset = col_offset(col);
        int w = 1;
        while (col + w < width() && is_trailing(static_cast<uint16_t>(col + w)))
            ++w;
        return w;
    }

    // ── 属性 ──
    text_attribute attr_at(uint16_t col) const noexcept
    {
        return (col < _attrs.size()) ? _attrs[col] : text_attribute{};
    }

    void set_attr(uint16_t col, text_attribute a) noexcept
    {
        if (col < _attrs.size())
            _attrs[col] = a;
    }

    void fill_attrs(uint16_t start, uint16_t end_excl, text_attribute a) noexcept
    {
        // end_excl 是半开区间右边界；超过行宽的部分静默截断，匹配控制台 API
        // 对短写的容忍行为。
        for (uint16_t c = start; c < end_excl && c < _attrs.size(); ++c)
            _attrs[c] = a;
    }

    bool try_write_single_width_run(uint16_t col, std::u32string_view text, text_attribute attr)
    {
        if (text.empty())
            return true;
        if (col >= width() || text.size() > static_cast<size_t>(width() - col))
            return false;

        const auto count = static_cast<uint16_t>(text.size());
        for (uint16_t i = 0; i < count; ++i)
        {
            const auto cell = static_cast<uint16_t>(col + i);
            const auto next = static_cast<uint16_t>(cell + 1);
            if (is_trailing(cell))
                return false;
            if (next < width() && is_trailing(next))
                return false;
            const auto start = col_offset(cell);
            const auto end = col_offset(next);
            if (end != start + 1 || start >= _text.size())
                return false;
        }

        for (uint16_t i = 0; i < count; ++i)
        {
            const auto cell = static_cast<uint16_t>(col + i);
            _text[col_offset(cell)] = text[i];
            _attrs[cell] = attr;
        }
        return true;
    }

    // ── 写入单 glyph (grapheme cluster) ──────────────
    // text: Unicode 码点序列 (可包含多个 char32_t 如 ZWJ 序列)
    // width_columns: 占据的列数 (1 或 2)
    void write_glyph(uint16_t col, std::u32string_view text, int width_columns, text_attribute attr)
    {
        COREHOST_PERF_SCOPE_AMOUNT(row_write_glyph, text.size());
        if (col >= width() || width_columns <= 0)
            return;
        if (col + width_columns > width())
            width_columns = width() - col;

        if (width_columns == 1 && text.size() == 1 && !is_trailing(col))
        {
            const auto next_col = static_cast<uint16_t>(col + 1);
            if (next_col <= width() && (next_col == width() || !is_trailing(next_col)))
            {
                const auto old_start = col_offset(col);
                const auto old_end = col_offset(next_col);
                if (old_end == old_start + 1 && old_start < _text.size())
                {
                    _text[old_start] = text[0];
                    set_attr(col, attr);
                    return;
                }
            }
        }

        // 如果 col 落在旧双宽 glyph 的 trailing 半列，必须先解除旧宽字符的列
        // 关系，否则新 glyph 会和旧 leading 列共享偏移。
        _unwrap_glyph(col);

        uint16_t old_start = col_offset(col);
        // old_end 是被替换 glyph 在 _text 中的结束偏移；用下一个非 trailing
        // 列的 offset 推导，最后一列由 _columns[width()] sentinel 支持。
        uint16_t old_end = old_start;
        {
            uint16_t scan = col + 1;
            while (scan < width() && is_trailing(scan))
                ++scan;
            old_end = (scan <= width()) ? col_offset(scan) : static_cast<uint16_t>(_text.size());
        }

        // text.size() 必须能放入 OFFSET_MASK；当前屏幕模型把每个 glyph 的
        // codepoint 序列嵌在一行 _text 中，offset 只有 15 位可用。
        uint16_t new_len = static_cast<uint16_t>(text.size());
        // 替换 _text 中 [old_start, old_end) 为 text。replace 后，后续所有
        // 非 trailing 列的 offset 都要按 delta 平移。
        if (old_end > _text.size())
            old_end = static_cast<uint16_t>(_text.size());
        if (old_start > old_end)
            old_start = old_end;

        _text.replace(old_start, old_end - old_start, reinterpret_cast<const char32_t *>(text.data()), new_len);

        int16_t delta = static_cast<int16_t>(new_len) - static_cast<int16_t>(old_end - old_start);

        // 更新 _columns 中 >= col 且非 trailing 的条目的偏移量。trailing 列
        // 没有自己的文本起点，稍后按新宽度重新标记。
        for (uint16_t c = col; c <= width(); ++c)
        {
            if (c == col || !is_trailing(c))
            {
                auto &off = _columns[c];
                uint16_t raw = off & OFFSET_MASK;
                if (c == col)
                    raw = old_start; // leading column 始终指向 old_start
                else if (raw >= old_end && c > col)
                    raw = static_cast<uint16_t>(raw + delta);
                off = (off & TRAILING_FLAG) | raw;
            }
        }

        // 设置宽度标记：leading 列保留普通 offset，后续列设置 TRAILING_FLAG
        // 并指向同一个 offset。
        for (int w = 1; w < width_columns; ++w)
        {
            uint16_t tc = static_cast<uint16_t>(col + w);
            _columns[tc] = _columns[col] | TRAILING_FLAG;
        }

        // 设置属性
        for (int w = 0; w < width_columns; ++w)
            set_attr(static_cast<uint16_t>(col + w), attr);

        // 修复 past-the-end 偏移
        _columns[width()] = static_cast<uint16_t>(_text.size());
    }

    void write_measured_run(uint16_t col, std::u32string_view text, std::span<const uint8_t> widths,
                            uint16_t total_columns, bool all_single_width, text_attribute attr)
    {
        COREHOST_PERF_SCOPE_AMOUNT(row_write_measured_run, text.size());
        assert(text.size() == widths.size());
        assert(col < width());
        assert(total_columns > 0);
        assert(col + total_columns <= width());
        if (text.empty())
            return;

        if (all_single_width && try_write_single_width_run(col, text, attr))
            return;

        _unwrap_glyph(col);
        const auto end_col = static_cast<uint16_t>(col + total_columns);
        if (end_col < width())
            _unwrap_glyph(end_col);

        const auto old_start = col_offset(col);
        const auto old_end = end_col <= width() ? col_offset(end_col) : static_cast<uint16_t>(_text.size());
        const auto old_len = static_cast<uint16_t>(old_end >= old_start ? old_end - old_start : 0);
        const auto new_len = static_cast<uint16_t>(text.size());

        _text.replace(old_start, old_len, text.data(), new_len);
        const auto delta = static_cast<int16_t>(new_len) - static_cast<int16_t>(old_len);

        for (uint16_t c = end_col; c <= width(); ++c)
        {
            if (c == width() || !is_trailing(c))
            {
                auto &off = _columns[c];
                uint16_t raw = off & OFFSET_MASK;
                if (raw >= old_end)
                    raw = static_cast<uint16_t>(raw + delta);
                off = (off & TRAILING_FLAG) | raw;
            }
        }

        uint16_t cell = col;
        uint16_t text_offset = old_start;
        for (size_t i = 0; i < text.size() && cell < width(); ++i, ++text_offset)
        {
            const auto width_columns = widths[i];
            _columns[cell] = text_offset;
            for (uint8_t w = 1; w < width_columns && cell + w < width(); ++w)
                _columns[static_cast<uint16_t>(cell + w)] = text_offset | TRAILING_FLAG;
            for (uint8_t w = 0; w < width_columns && cell + w < width(); ++w)
                set_attr(static_cast<uint16_t>(cell + w), attr);
            cell = static_cast<uint16_t>(cell + width_columns);
        }

        _columns[width()] = static_cast<uint16_t>(_text.size());
    }

    // ── 清除指定列 ──
    void clear_cell(uint16_t col, text_attribute attr = text_attribute{})
    {
        write_glyph(col, U" ", 1, attr);
    }

    // ── 从另一行拷贝一段列 ──
    void copy_from(const screen_buffer_row &src, uint16_t src_start, uint16_t dst_start, uint16_t count)
    {
        // 按列复制时只从 leading 列复制 glyph。遇到 trailing 列跳过，避免把
        // 一个双宽字符的后半列错误写成独立字符。
        uint16_t w = width();
        uint16_t sw = src.width();
        for (uint16_t i = 0; i < count && dst_start + i < w && src_start + i < sw; ++i)
        {
            uint16_t sc = static_cast<uint16_t>(src_start + i);
            if (src.is_trailing(sc))
                // trailing 列由 leading glyph 一次写入，不能单独复制。
                continue;
            write_glyph(static_cast<uint16_t>(dst_start + i), src.glyph_at(sc), src.glyph_width(sc), src.attr_at(sc));
        }
    }

    // ── 整行填充 ──
    void fill(std::u32string_view text, text_attribute attr)
    {
        if (text.empty())
            return;
        // fill 当前按单列重复 text；如果 text 本身代表宽 glyph，调用者应改用
        // write_glyph 并传入正确 width。
        uint16_t w = width();
        for (uint16_t col = 0; col < w;)
        {
            write_glyph(col, text, 1, attr);
            ++col;
        }
    }

    // ── CHAR_INFO 级别导出 (供 API 边界) ──
    // 将此行写入 CHAR_INFO 数组 (调用者保证 out 至少 width() 个)
    void to_char_info(CHAR_INFO *out) const noexcept
    {
        uint16_t w = width();
        for (uint16_t col = 0; col < w; ++col)
        {
            // CHAR_INFO 只能容纳一个 UTF-16 code unit 和属性。内部多 codepoint
            // glyph 在 API 边界降级为首码点，非 BMP 使用 U+FFFD。
            auto &ci = out[col];
            auto gv = glyph_at(col);
            if (!gv.empty())
            {
                // char32_t → wchar_t (取首字符, 单宽度情况下一个 cluster 通常只有一个 BMP 字符)
                // 若 cluster 有多个 char32_t (含代理对/ZWJ), 仅取首字符填入 CHAR_INFO
                // 第二列若是 trailing 填 0x0000 (由 DbcsAttr 标记)
                ci.Char.UnicodeChar = static_cast<wchar_t>((gv[0] <= 0xFFFF) ? gv[0] : 0xFFFD);
            }
            else
            {
                ci.Char.UnicodeChar = L' ';
            }
            ci.Attributes = attr_at(col).legacy;
        }
    }

    // ── 从 CHAR_INFO 数组导入 ──
    void from_char_info(const CHAR_INFO *src, uint16_t count, uint16_t dst_col = 0)
    {
        for (uint16_t i = 0; i < count && dst_col + i < width(); ++i)
        {
            wchar_t wch = src[i].Char.UnicodeChar;
            if (wch == 0)
                continue; // trailing 标记, 跳过
            // CHAR_INFO 不携带完整 surrogate pair；孤立 high surrogate 不能直接
            // 进入 char32_t 屏幕模型，使用替换字符。
            char32_t cp = (wch >= 0xD800 && wch <= 0xDBFF) ? 0xFFFD // 孤立 UTF-16 high surrogate
                                                           : static_cast<char32_t>(wch);
            write_glyph(static_cast<uint16_t>(dst_col + i), std::u32string_view{&cp, 1}, 1,
                        text_attribute{src[i].Attributes});
        }
    }

  private:
    // 清除 col 处 glyph 的 trailing 部分 (解除宽字符的后半列标记)
    void _unwrap_glyph(uint16_t col)
    {
        // 如果 col 是 trailing 列，从当前位置向右清掉连续 trailing 标记。
        // 这让后续 write_glyph 可以把 col 当作新的 leading 列。
        uint16_t w = width();
        uint16_t scan = col;
        while (scan < w && is_trailing(scan))
        {
            _columns[scan] &= OFFSET_MASK;
            ++scan;
        }
        // 如果刚才从 trailing 区中间开始，scan 会停在旧 glyph 后的 leading。
        // 继续清理该 leading 后面的 trailing，避免旧宽度关系残留。
        if (scan < w && scan > col)
        {
            uint16_t end = scan;
            while (end < w && is_trailing(end))
                ++end;
            for (uint16_t c = scan; c < end; ++c)
                _columns[c] &= OFFSET_MASK;
        }
    }
};

} // namespace conpty
