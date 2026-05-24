// ── conpty/screen_buffer_row.hpp ───────────────────
// 屏幕缓冲区行存储 — 对标原始 terminal/src/buffer/out/Row.hpp
//
// 设计:
//   _text:    std::u32string — 该行全部 char32_t 文本 (连续, 无 padding null)
//   _columns: std::vector<uint16_t> — 列→_text 偏移量, 0x8000 标记宽字符的后半列
//   _attrs:   std::vector<WORD> — 每列的传统属性 (16 色)
//
// graphene cluster 支持:
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
#include <string>
#include <vector>
#include <string_view>
#include <algorithm>

namespace conpty
{

// ── TextAttribute (对标原始 TextAttribute, 简化版) ──
struct text_attribute
{
    WORD legacy = 0x07; // 16 色传统属性

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
    static constexpr uint16_t TRAILING_FLAG = 0x8000;
    static constexpr uint16_t OFFSET_MASK = 0x7FFF;

    std::u32string _text;               // char32_t 文本 (连续)
    std::vector<uint16_t> _columns;     // 列→偏移 [0..width], _columns[width] 为 past-the-end
    std::vector<text_attribute> _attrs; // 每列属性

    screen_buffer_row() = default;

    // ── 构造: 填充 width 列 (默认空格, 默认属性) ──
    explicit screen_buffer_row(uint16_t width, WORD default_attr = 0x07)
    {
        _columns.resize(static_cast<size_t>(width) + 1);
        _attrs.resize(width, text_attribute{default_attr});
        // 所有列指向同一个空格字符
        _text.assign(static_cast<size_t>(width), U' ');
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
        // 跳过 trailing column 找到下一个非 trailing 列的真实偏移
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
        // Count consecutive columns sharing the same offset
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
        for (uint16_t c = start; c < end_excl && c < _attrs.size(); ++c)
            _attrs[c] = a;
    }

    // ── 写入单 glyph (grapheme cluster) ──────────────
    // text: Unicode 码点序列 (可包含多个 char32_t 如 ZWJ 序列)
    // width_columns: 占据的列数 (1 或 2)
    void write_glyph(uint16_t col, std::u32string_view text, int width_columns, text_attribute attr)
    {
        if (col >= width() || width_columns <= 0)
            return;
        if (col + width_columns > width())
            width_columns = width() - col;

        // 清除旧 glyph 的 trailing 标记并取得可用的 text 范围
        _unwrap_glyph(col);

        uint16_t old_start = col_offset(col);
        // old_end = 下一个非 trailing 列的起始偏移 (或 _text.size())
        uint16_t old_end = old_start;
        {
            uint16_t scan = col + 1;
            while (scan < width() && is_trailing(scan))
                ++scan;
            old_end = (scan <= width()) ? col_offset(scan) : static_cast<uint16_t>(_text.size());
        }

        uint16_t new_len = static_cast<uint16_t>(text.size());
        // 替换 _text 中 [old_start, old_end) 为 text
        if (old_end > _text.size())
            old_end = static_cast<uint16_t>(_text.size());
        if (old_start > old_end)
            old_start = old_end;

        _text.replace(old_start, old_end - old_start, reinterpret_cast<const char32_t *>(text.data()), new_len);

        int16_t delta = static_cast<int16_t>(new_len) - static_cast<int16_t>(old_end - old_start);

        // 更新 _columns 中 >= col 且非 trailing 的条目的偏移量
        // 注意: 先清掉 col 处的 trailing, 再按 width_columns 设置
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

        // 设置宽度标记
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

    // ── 清除指定列 ──
    void clear_cell(uint16_t col, text_attribute attr = text_attribute{})
    {
        write_glyph(col, U" ", 1, attr);
    }

    // ── 从另一行拷贝一段列 ──
    void copy_from(const screen_buffer_row &src, uint16_t src_start, uint16_t dst_start, uint16_t count)
    {
        uint16_t w = width();
        uint16_t sw = src.width();
        for (uint16_t i = 0; i < count && dst_start + i < w && src_start + i < sw; ++i)
        {
            uint16_t sc = static_cast<uint16_t>(src_start + i);
            if (src.is_trailing(sc))
                continue; // skip trailing halves
            write_glyph(static_cast<uint16_t>(dst_start + i), src.glyph_at(sc), src.glyph_width(sc), src.attr_at(sc));
        }
    }

    // ── 整行填充 ──
    void fill(std::u32string_view text, text_attribute attr)
    {
        if (text.empty())
            return;
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
                continue;                                           // trailing 标记, 跳过
            char32_t cp = (wch >= 0xD800 && wch <= 0xDBFF) ? 0xFFFD // 孤立代理
                                                           : static_cast<char32_t>(wch);
            write_glyph(static_cast<uint16_t>(dst_col + i), std::u32string_view{&cp, 1}, 1,
                        text_attribute{src[i].Attributes});
        }
    }

  private:
    // 清除 col 处 glyph 的 trailing 部分 (解除宽字符的后半列标记)
    void _unwrap_glyph(uint16_t col)
    {
        // 找到此 glyph 覆盖的所有 trailing 列并清除标记
        uint16_t w = width();
        uint16_t scan = col;
        while (scan < w && is_trailing(scan))
        {
            _columns[scan] &= OFFSET_MASK;
            ++scan;
        }
        // 再往后找到真正的 leading 之后的所有 trailing
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
