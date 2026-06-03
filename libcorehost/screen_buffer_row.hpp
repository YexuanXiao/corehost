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
#include <numeric>
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

    // true 表示每个可见列都对应 _text 中同下标的单个 codepoint，_columns
    // 可以不被读取；一旦写入双宽 glyph 或多 codepoint glyph 就切换为 false。
    bool _single_width_layout = false;

    // true 表示整行尚未写入 _text，当前行等价于 width 个 _fill_char 和
    // _fill_attr。写入局部内容前会物化，整行清除/滚动填充会重新进入该状态。
    bool _filled = false;
    char32_t _fill_char = U' ';
    text_attribute _fill_attr{};

    // true 表示整行属性都等于 _uniform_attr，_attrs 的具体内容可以暂不维护；
    // 对局部属性写入会物化，整行属性覆盖会重新进入该状态。
    bool _attrs_uniform = false;
    text_attribute _uniform_attr{};

    screen_buffer_row() = default;

    // ── 构造: 填充 width 列 (默认空格, 默认属性) ──
    explicit screen_buffer_row(uint16_t width, WORD default_attr = 0x07)
    {
        reset_fill(width, U' ', text_attribute{default_attr});
    }

    // 将行重置为“整行同一字符/属性”的压缩状态。调用后 width() 等于
    // width，读取路径能直接从 _fill_char/_fill_attr 得到内容，后续局部写入
    // 再按需要物化 _text/_attrs。
    void reset_fill(uint16_t width, char32_t cp, text_attribute attr)
    {
        _columns.resize(static_cast<size_t>(width) + 1);
        _attrs.resize(width);
        _fill_char = cp;
        _fill_attr = attr;
        _filled = true;
        _single_width_layout = true;
        _attrs_uniform = true;
        _uniform_attr = attr;
    }

    // ── 基本信息 ──
    // 返回当前行的可见列数。_columns 必须保留一个 sentinel，所以真实宽度为
    // _columns.size()-1；空行只会出现在默认构造后或尚未初始化时。
    uint16_t width() const noexcept
    {
        return static_cast<uint16_t>(_columns.empty() ? 0 : _columns.size() - 1);
    }

    // ── 偏移量读取 ──
    // 返回 col 对应 glyph 在 _text 中的起始偏移，不带 TRAILING_FLAG。
    // 对 _filled/_single_width_layout 状态，列下标本身就是逻辑偏移。
    uint16_t col_offset(uint16_t col) const noexcept
    {
        // 返回值已经去掉 trailing 标志；越界列按 0 处理，调用者通常会先用
        // width() 做边界检查。
        if (_filled)
            return col <= width() ? col : 0;
        if (_single_width_layout)
            return col <= width() ? col : 0;
        return (col < _columns.size()) ? (_columns[col] & OFFSET_MASK) : 0;
    }

    // 判断 col 是否是宽 glyph 的后续列。trailing 列不能作为独立写入起点，
    // 写入前必须先解除旧 glyph 的列关系。
    bool is_trailing(uint16_t col) const noexcept
    {
        if (_filled || _single_width_layout)
            return false;
        return (col < _columns.size()) && (_columns[col] & TRAILING_FLAG);
    }

    // ── 字素簇 (glyph/grapheme cluster) 读取 ──
    // 返回 col 所在 glyph 的完整 codepoint 序列；如果 col 是 trailing 列，
    // 也返回对应 leading glyph 的序列，便于 CHAR_INFO 导出和区域复制。
    std::u32string_view glyph_at(uint16_t col) const noexcept
    {
        if (col >= width())
            return {};
        if (_filled)
            return std::u32string_view{&_fill_char, 1};
        if (_single_width_layout)
            return col < _text.size() ? std::u32string_view{_text.data() + col, 1} : std::u32string_view{};
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

    // 返回 col 处 glyph 占据的屏幕列数。trailing 列返回 0，因为宽度属于
    // leading 列；调用者按列扫描时可据此跳过后续列。
    int glyph_width(uint16_t col) const noexcept
    {
        if (col >= width())
            return 0;
        if (_filled || _single_width_layout)
            return 1;
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
    // 返回 col 的 Win32 属性。属性按可见列保存，所以 trailing 列也有独立
    // 属性值；越界读取返回默认属性，避免 API 读路径访问无效内存。
    text_attribute attr_at(uint16_t col) const noexcept
    {
        if (_attrs_uniform && col < width())
            return _uniform_attr;
        return (col < _attrs.size()) ? _attrs[col] : text_attribute{};
    }

    // 修改单列属性。局部修改会强制物化 _filled/_attrs_uniform，否则无法在
    // 压缩状态下表达“只有一列不同”。
    void set_attr(uint16_t col, text_attribute a) noexcept
    {
        materialize_filled();
        materialize_attrs();
        if (col < _attrs.size())
            _attrs[col] = a;
    }

    // 修改半开列区间 [start, end_excl) 的属性。整行覆盖会保留 uniform
    // 压缩状态；局部覆盖会物化 _attrs 并对越界右边界截断。
    void fill_attrs(uint16_t start, uint16_t end_excl, text_attribute a) noexcept
    {
        materialize_filled();
        if (start == 0 && end_excl >= width())
        {
            _attrs_uniform = true;
            _uniform_attr = a;
            return;
        }
        materialize_attrs();
        // end_excl 是半开区间右边界；超过行宽的部分静默截断，匹配控制台 API
        // 对短写的容忍行为。
        if (start >= _attrs.size())
            return;
        const auto end = std::min<size_t>(end_excl, _attrs.size());
        std::fill(_attrs.begin() + start, _attrs.begin() + end, a);
    }

    // 快速写入全单宽文本段。成功要求目标列区间内没有宽 glyph 的 leading/
    // trailing 结构，也没有多 codepoint glyph；失败不改变行内容，调用方应
    // 改用 write_measured_run。
    bool try_write_single_width_run(uint16_t col, std::u32string_view text, text_attribute attr)
    {
        materialize_filled();
        if (text.empty())
            return true;
        if (col >= width() || text.size() > static_cast<size_t>(width() - col))
            return false;

        if (_single_width_layout)
        {
            assert(_text.size() == width());
            std::copy(text.begin(), text.end(), _text.begin() + col);
            write_attr_range(col, static_cast<uint16_t>(col + text.size()), attr);
            return true;
        }

        const auto count = static_cast<uint16_t>(text.size());
        for (uint16_t i = 0; i != count; ++i)
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

        for (uint16_t i = 0; i != count; ++i)
        {
            const auto cell = static_cast<uint16_t>(col + i);
            _text[col_offset(cell)] = text[i];
        }
        write_attr_range(col, static_cast<uint16_t>(col + count), attr);
        return true;
    }

    // ── 写入单 glyph (grapheme cluster) ──────────────
    // text: Unicode 码点序列，可包含多个 char32_t；width_columns 是上层已经
    // 计算好的可见列宽。该函数会修正被覆盖区域两端的旧宽 glyph 关系，并
    // 维护 _text offset、trailing 标记和属性同步。
    void write_glyph(uint16_t col, std::u32string_view text, int width_columns, text_attribute attr)
    {
        COREHOST_PERF_SCOPE_AMOUNT(row_write_glyph, text.size());
        if (col >= width() || width_columns <= 0)
            return;
        if (col + width_columns > width())
            width_columns = width() - col;
        materialize_filled();

        if (_single_width_layout && width_columns == 1 && text.size() == 1)
        {
            assert(_text.size() == width());
            _text[col] = text[0];
            write_attr_range(col, static_cast<uint16_t>(col + 1), attr);
            return;
        }

        if (_single_width_layout)
            materialize_columns_from_single_width_layout();

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
        _single_width_layout = false;
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

    // 在新文本的列布局与旧布局完全一致时原地替换 _text。这个路径不移动
    // _columns offset，适合重复刷新同一位置的 CJK/宽字符文本；返回 false
    // 表示布局不同或边界不满足，调用方必须走会重建列映射的路径。
    bool try_overwrite_matching_measured_run(uint16_t col, std::u32string_view text, std::span<const char8_t> widths,
                                             uint16_t total_columns, text_attribute attr)
    {
        const auto end_col = static_cast<uint16_t>(col + total_columns);
        if (_filled)
            return false;
        if (text.empty() || col >= width() || end_col > width() || is_trailing(col))
            return false;

        const auto old_start = col_offset(col);
        if (old_start + text.size() > _text.size())
            return false;

        uint16_t cell = col;
        for (size_t i = 0; i != text.size(); ++i)
        {
            if (cell >= width() || is_trailing(cell) || col_offset(cell) != old_start + i)
                return false;

            const auto width_columns = static_cast<uint8_t>(widths[i]);
            if (width_columns == 0 || cell + width_columns > width())
                return false;

            for (uint8_t w = 1; w < width_columns; ++w)
            {
                const auto trailing_cell = static_cast<uint16_t>(cell + w);
                if (!is_trailing(trailing_cell) || col_offset(trailing_cell) != old_start + i)
                    return false;
            }
            cell = static_cast<uint16_t>(cell + width_columns);
        }

        if (cell != end_col || col_offset(end_col) != old_start + text.size())
            return false;

        COREHOST_PERF_SCOPE_AMOUNT(row_write_matching_layout, text.size());
        std::copy(text.begin(), text.end(), _text.begin() + old_start);
        write_attr_range(col, end_col, attr);
        return true;
    }

    // 在整行仍处于 _filled 压缩状态时写入已测量文本段。函数直接构造最终
    // _text/_columns，避免先物化整行再替换；成功后行不再是 _filled。
    bool try_write_measured_run_on_filled_row(uint16_t col, std::u32string_view text, std::span<const char8_t> widths,
                                              uint16_t total_columns, bool all_single_width, text_attribute attr)
    {
        const auto row_width = width();
        const auto end_col = static_cast<uint16_t>(col + total_columns);
        if (!_filled || text.empty() || col >= row_width || end_col > row_width)
            return false;
        assert(all_single_width || text.size() == widths.size());
        COREHOST_PERF_SCOPE_AMOUNT(row_write_filled, text.size());

        if (all_single_width)
        {
            _text.clear();
            _text.reserve(row_width);
            _text.append(col, _fill_char);
            _text.append(text.data(), text.size());
            _text.append(static_cast<size_t>(row_width - end_col), _fill_char);
            std::iota(_columns.begin(), _columns.end(), uint16_t{0});
        }
        else
        {
            _text.clear();
            _text.reserve(static_cast<size_t>(row_width) - total_columns + text.size());
            _text.append(static_cast<size_t>(col), _fill_char);
            _text.append(text.data(), text.size());
            _text.append(static_cast<size_t>(row_width - end_col), _fill_char);

            std::iota(_columns.begin(), _columns.begin() + col, uint16_t{0});

            uint16_t cell = col;
            uint16_t text_offset = col;
            for (size_t i = 0; i != text.size(); ++i, ++text_offset)
            {
                const auto width_columns = static_cast<uint8_t>(widths[i]);
                _columns[cell] = text_offset;
                for (uint8_t w = 1; w < width_columns; ++w)
                    _columns[static_cast<uint16_t>(cell + w)] = text_offset | TRAILING_FLAG;
                cell = static_cast<uint16_t>(cell + width_columns);
            }
            assert(cell == end_col);

            const auto suffix_offset = static_cast<uint16_t>(col + text.size());
            std::iota(_columns.begin() + end_col, _columns.begin() + row_width + 1, suffix_offset);
        }

        _attrs_uniform = true;
        _uniform_attr = _fill_attr;
        write_attr_range(col, end_col, attr);
        _filled = false;
        _single_width_layout = all_single_width;
        return true;
    }

    // 在旧行是纯单宽布局时写入包含宽度信息的文本段。该路径从单宽 offset
    // 直接重建受影响区域和后缀 sentinel；如果输入仍是单宽，调用方会优先走
    // try_write_single_width_run。
    bool try_write_measured_run_on_single_width_layout(uint16_t col, std::u32string_view text,
                                                       std::span<const char8_t> widths, uint16_t total_columns,
                                                       text_attribute attr)
    {
        const auto row_width = width();
        const auto end_col = static_cast<uint16_t>(col + total_columns);
        if (text.empty() || col >= row_width || end_col > row_width || text.size() != widths.size())
            return false;
        if (_filled || !_single_width_layout)
            return false;
        assert(_text.size() == row_width);
        COREHOST_PERF_SCOPE_AMOUNT(row_write_single_layout, text.size());

        std::iota(_columns.begin(), _columns.end(), uint16_t{0});
        _text.replace(col, total_columns, text.data(), text.size());

        uint16_t cell = col;
        auto text_offset = col;
        for (size_t i = 0; i != text.size(); ++i, ++text_offset)
        {
            const auto width_columns = static_cast<uint8_t>(widths[i]);
            _columns[cell] = static_cast<uint16_t>(text_offset);
            for (uint8_t w = 1; w < width_columns; ++w)
                _columns[static_cast<uint16_t>(cell + w)] =
                    static_cast<uint16_t>(text_offset) | TRAILING_FLAG;
            cell = static_cast<uint16_t>(cell + width_columns);
        }
        assert(cell == end_col);

        const auto suffix_offset = static_cast<uint16_t>(col + text.size());
        std::iota(_columns.begin() + end_col, _columns.begin() + row_width + 1, suffix_offset);

        write_attr_range(col, end_col, attr);
        _single_width_layout = false;
        return true;
    }

    // 写入一段已经测量过宽度的文本。widths 只在 all_single_width=false 时
    // 与 text 一一对应；total_columns 是文本占据的可见列数。函数按常见度
    // 选择压缩行、整行替换、单宽替换、同布局替换，最后才走通用 offset
    // 重建路径。
    void write_measured_run(uint16_t col, std::u32string_view text, std::span<const char8_t> widths,
                            uint16_t total_columns, bool all_single_width, text_attribute attr)
    {
        COREHOST_PERF_SCOPE_AMOUNT(row_write_measured_run, text.size());
        assert(all_single_width || text.size() == widths.size());
        assert(col < width());
        assert(total_columns > 0);
        assert(col + total_columns <= width());
        if (text.empty())
            return;

        if (try_write_measured_run_on_filled_row(col, text, widths, total_columns, all_single_width, attr))
            return;

        if (col == 0 && total_columns == width())
        {
            COREHOST_PERF_SCOPE_AMOUNT(row_write_full_row, text.size());
            _filled = false;
            _text.assign(text.data(), text.size());
            if (!all_single_width)
            {
                uint16_t cell = 0;
                for (uint16_t text_offset = 0; text_offset < text.size(); ++text_offset)
                {
                    const auto width_columns = static_cast<uint8_t>(widths[text_offset]);
                    _columns[cell] = text_offset;
                    for (uint8_t w = 1; w < width_columns; ++w)
                        _columns[static_cast<uint16_t>(cell + w)] = text_offset | TRAILING_FLAG;
                    cell = static_cast<uint16_t>(cell + width_columns);
                }
                assert(cell == width());
                _columns[width()] = static_cast<uint16_t>(_text.size());
            }
            _attrs_uniform = true;
            _uniform_attr = attr;
            _single_width_layout = all_single_width;
            return;
        }

        if (all_single_width && try_write_single_width_run(col, text, attr))
            return;
        if (!all_single_width && try_write_measured_run_on_single_width_layout(col, text, widths, total_columns, attr))
            return;
        if (!all_single_width && try_overwrite_matching_measured_run(col, text, widths, total_columns, attr))
            return;

        COREHOST_PERF_SCOPE_AMOUNT(row_write_generic, text.size());
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
        for (size_t i = 0; i != text.size(); ++i, ++text_offset)
        {
            const auto width_columns = static_cast<uint8_t>(widths[i]);
            _columns[cell] = text_offset;
            for (uint8_t w = 1; w < width_columns; ++w)
                _columns[static_cast<uint16_t>(cell + w)] = text_offset | TRAILING_FLAG;
            cell = static_cast<uint16_t>(cell + width_columns);
        }
        write_attr_range(col, end_col, attr);
        assert(cell == end_col);

        _columns[width()] = static_cast<uint16_t>(_text.size());
        _single_width_layout = false;
    }

    // ── 清除指定列 ──
    // 使用空格覆盖指定列；如果该列位于宽 glyph 上，write_glyph 会拆开旧
    // glyph 关系，保证清除后该列成为独立单宽空格。
    void clear_cell(uint16_t col, text_attribute attr = text_attribute{})
    {
        write_glyph(col, U" ", 1, attr);
    }

    // ── 从另一行拷贝一段列 ──
    // 按可见列复制 src 的一段内容到当前行。count 是列数而不是 codepoint
    // 数；双宽 glyph 只在 leading 列复制一次。
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
    // 用同一个单列 glyph 重复覆盖整行。当前只用于简单填充路径；复杂宽度
    // glyph 不应走这里，否则行宽语义会和调用者预期不一致。
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
    // 将内部 glyph/属性模型降级到 Win32 CHAR_INFO。调用者保证 out 至少有
    // width() 个元素；导出不会改变行内压缩状态。
    void to_char_info(CHAR_INFO *out) const noexcept
    {
        uint16_t w = width();
        for (uint16_t col = 0; col != w; ++col)
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
    // 从 Win32 CHAR_INFO 写回内部行状态。CHAR_INFO 只能携带单个 UTF-16
    // code unit，因此无法恢复完整 grapheme；dst_col 是目标起始列。
    void from_char_info(const CHAR_INFO *src, uint16_t count, uint16_t dst_col = 0)
    {
        materialize_filled();
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
    // 将 _filled 压缩行展开为 _text 中的 width 个单宽填充字符。展开后仍
    // 保持属性 uniform，避免不必要地填充 _attrs。
    void materialize_filled()
    {
        if (!_filled)
            return;

        const auto row_width = width();
        _text.assign(row_width, _fill_char);
        _attrs_uniform = true;
        _uniform_attr = _fill_attr;
        _filled = false;
        _single_width_layout = true;
    }

    // 为纯单宽布局生成显式 _columns。只有当后续写入需要表达宽 glyph 或
    // 多 codepoint glyph 时才需要这一步。
    void materialize_columns_from_single_width_layout()
    {
        if (!_single_width_layout)
            return;

        assert(_text.size() == width());
        std::iota(_columns.begin(), _columns.end(), uint16_t{0});
        _single_width_layout = false;
    }

    // 将 uniform 属性展开到 _attrs。局部属性修改必须调用它；整行属性读取
    // 和整行覆盖可以继续使用 _uniform_attr。
    void materialize_attrs()
    {
        if (!_attrs_uniform)
            return;

        std::fill(_attrs.begin(), _attrs.end(), _uniform_attr);
        _attrs_uniform = false;
    }

    // 写入属性半开区间。整行覆盖直接切换为 uniform 状态；如果写入属性与
    // 当前 uniform 属性相同，则无需物化。
    void write_attr_range(uint16_t start, uint16_t end_excl, text_attribute attr)
    {
        if (start == 0 && end_excl >= width())
        {
            _attrs_uniform = true;
            _uniform_attr = attr;
            return;
        }
        if (_attrs_uniform && attr == _uniform_attr)
            return;

        materialize_attrs();
        if (start >= _attrs.size())
            return;

        const auto end = std::min<size_t>(end_excl, _attrs.size());
        std::fill(_attrs.begin() + start, _attrs.begin() + end, attr);
    }

    // 解除 col 所在旧 glyph 的 trailing 关系。写入新 glyph 前必须让被覆盖
    // 边界两侧不再共享旧 offset，否则新旧宽字符会互相污染列映射。
    void _unwrap_glyph(uint16_t col)
    {
        uint16_t w = width();
        if (col >= w)
            return;

        if (is_trailing(col))
        {
            uint16_t scan = col;
            while (scan < w && is_trailing(scan))
            {
                _columns[scan] &= OFFSET_MASK;
                ++scan;
            }
            return;
        }

        for (uint16_t scan = static_cast<uint16_t>(col + 1); scan < w && is_trailing(scan); ++scan)
        {
            _columns[scan] &= OFFSET_MASK;
        }
    }
};

} // namespace conpty
