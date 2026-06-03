// ── conpty/screen_buffer.hpp ──────────────────────
// 屏幕缓冲区。
//
// 功能分解：
// 1. 以 screen_buffer_row 保存每行 glyph、列偏移和属性。
// 2. 对 Console API 暴露 char32_t/WORD 操作，并在 row_to_ci/row_from_ci
//    边界转换 CHAR_INFO。
// 3. resize/scroll/fill 只改变本地缓冲区；VT 同步由 api_handlers 或
//    vt_msg_dispatch 负责。
#pragma once
#include <windows.h>
#include <algorithm>
#include <cstring>
#include <ranges>
#include <vector>
#include "default_console_size.hpp"
#include "console_viewport.hpp"
#include "screen_buffer_row.hpp"
#include "char_convert.hpp"
#include "char_width.hpp"
#include "perf_diag.hpp"
#include "utility/raw_byte_allocator.hpp"

namespace conpty
{

struct screen_buffer
{
    struct text_row_write_result
    {
        // consumed 是本次从输入 text 中实际写入当前行的 codepoint 数；调用方
        // 用它 remove_prefix 后继续处理剩余文本。
        size_t consumed = 0;
        // row_end=true 表示写入推进到了行尾，调用方需要执行终端换行/滚动逻辑。
        bool row_end = false;
    };

    // 字符列/行数，必须保持 >= 1。resize 会修正非法输入。
    COORD size{default_console_size};
    console_viewport viewport{default_console_size};

    // 构造默认 120x30 缓冲区；_ensure_rows 会建立与 size 匹配的空白行。
    screen_buffer()
    {
        _ensure_rows();
    }
    // 构造指定尺寸的缓冲区。sz 会作为本地 API 模型和初始 viewport 的尺寸；
    // 非法尺寸由调用 resize 的路径修正，直接构造者应传入正尺寸。
    explicit screen_buffer(COORD sz) : size(sz), viewport(sz)
    {
        _ensure_rows();
    }

    // 调整本地屏幕缓冲区尺寸，并尽量保留左上角重叠区域的内容。resize 会
    // 先线性化环形行存储，保证 old_rows[y] 仍对应逻辑 y。
    void resize(COORD new_size)
    {
        // new_size 来自 SetConsoleScreenBufferInfo/WT resize/会话初始化；本函数
        // 只改变本地 API 可见模型，不发任何 VT 序列。
        // 控制台缓冲区不能为 0 行或 0 列。
        if (new_size.X < 1)
            new_size.X = 1;
        if (new_size.Y < 1)
            new_size.Y = 1;
        if (new_size.X == size.X && new_size.Y == size.Y)
            return;

        _linearize_rows();

        // old_rows 保存 resize 前内容。resize 是本地模型变更，不主动发 VT；
        // 调用者若需要终端同步，必须在 API handler 中重绘或发 resize 序列。
        auto old_rows = std::move(_rows);
        auto old_size = size;
        size = new_size;
        viewport.clamp_to_buffer(size);
        _ensure_rows();

        // 只复制新旧高度重叠的行；copy_from 自己按目标宽度截断列。
        SHORT copy_h = old_size.Y < new_size.Y ? old_size.Y : new_size.Y;
        for (SHORT y = 0; y < copy_h; ++y)
            _rows[y].copy_from(old_rows[y], 0, 0, static_cast<uint16_t>(old_size.X));
    }

    // ── 行访问 ──
    // 返回逻辑行 y 对应的物理行。调用方必须传入有效 y；全屏垂直滚动后
    // _row_origin 可能非 0，因此不能直接索引 _rows[y]。
    screen_buffer_row &row(SHORT y) noexcept
    {
        return _rows[_physical_row_index(y)];
    }
    // const 版本的逻辑行访问，用于 API 读路径和 CHAR_INFO 导出。
    const screen_buffer_row &row(SHORT y) const noexcept
    {
        return _rows[_physical_row_index(y)];
    }

    // ── 单 glyph 读写 ──
    // write_glyph 用于调用者已经完成宽度测量的路径，避免同一个 codepoint 在
    // 上层排版和底层写入时重复计算宽度。
    void write_glyph(COORD c, std::u32string_view text, int width_columns, WORD attr = 0x07)
    {
        // c 是控制台缓冲区坐标，text 是一个完整 glyph/grapheme cluster。
        // width_columns 是该 glyph 占据的屏幕列数，通常由上层排版提前算好。
        COREHOST_PERF_SCOPE(screen_set_u32);
        if (!_valid(c) || text.empty())
            return;
        if (width_columns < 1)
            width_columns = 1;
        if (width_columns > 2)
            width_columns = 2;
        row(c.Y).write_glyph(static_cast<uint16_t>(c.X), text, width_columns, text_attribute{attr});
    }

    // 写入单个 codepoint，并按 console 宽度规则更新目标 cell。它是
    // FillConsoleOutput/WriteConsoleOutputString 等单字符 API 的基础路径。
    void set_u32(COORD c, char32_t cp, WORD attr = 0x07)
    {
        // Console 模式只区分 1/2 列宽；组合字符和控制字符在这里至少占一列，
        // 防止屏幕模型出现 0 宽单元格。
        int cw = char_width_for_mode(cp, text_measurement_mode::console);
        write_glyph(c, std::u32string_view{&cp, 1}, cw, attr);
    }

    // 尝试在一行内写入全单宽文本段。成功时直接更新目标行，失败表示调用方
    // 需要走带宽度数组的 write_text_row/write_measured_run。
    bool try_write_single_width_run(COORD c, std::u32string_view text, WORD attr = 0x07)
    {
        // 快路径只接受每个 codepoint 正好占一列的文本段；失败表示调用方应走
        // write_text_row 的宽度测量路径。
        if (!_valid(c))
            return false;
        return row(c.Y).try_write_single_width_run(static_cast<uint16_t>(c.X), text, text_attribute{attr});
    }

    // 写入一段不含控制字符的文本到当前 viewport 所在行。cursor 是输入/输出
    // 状态；返回值告诉调用方消费了多少 codepoint，以及是否需要换行/滚动。
    text_row_write_result write_text_row(COORD &cursor, std::u32string_view text, WORD attr,
                                         text_measurement_mode measurement, bool ambiguous_is_wide)
    {
        // cursor 是输入/输出参数：进入时为本地 Console 光标，返回时推进到本行
        // 写入结束位置。text 不跨越控制字符；attr 是本段写入的 Win32 属性。
        text_row_write_result result;
        if (text.empty())
            return result;

        const auto view = viewport.rect();
        cursor.X = std::clamp<SHORT>(cursor.X, view.Left, view.Right);
        cursor.Y = std::clamp<SHORT>(cursor.Y, view.Top, view.Bottom);

        _write_widths.clear();
        uint16_t cell_count = 0;
        bool all_single_width = true;
        while (result.consumed < text.size())
        {
            const auto ch = text[result.consumed];
            int width_columns = char_width_for_mode(ch, measurement, ambiguous_is_wide);
            if (width_columns < 1)
                width_columns = 1;
            if (width_columns > 2)
                width_columns = 2;

            if (cursor.X + cell_count + width_columns - 1 > view.Right)
                break;

            if (width_columns != 1)
            {
                if (all_single_width)
                    _write_widths.assign(result.consumed, char8_t{1});
                all_single_width = false;
            }
            if (!all_single_width)
                _write_widths.push_back(static_cast<char8_t>(width_columns));
            cell_count = static_cast<uint16_t>(cell_count + width_columns);
            ++result.consumed;
        }

        if (result.consumed != 0)
        {
            auto &target_row = row(cursor.Y);
            const auto target_col = static_cast<uint16_t>(cursor.X);
            const auto text_run = text.substr(0, result.consumed);
            const bool full_row_replace = target_col == 0 && cell_count == target_row.width();
            if (all_single_width)
            {
                if (full_row_replace)
                {
                    target_row.write_measured_run(target_col, text_run, {}, cell_count, true, text_attribute{attr});
                }
                else if (!target_row.try_write_single_width_run(target_col, text_run, text_attribute{attr}))
                {
                    _write_widths.assign(result.consumed, char8_t{1});
                    target_row.write_measured_run(target_col, text_run, _write_widths, cell_count, false,
                                                  text_attribute{attr});
                }
            }
            else
            {
                target_row.write_measured_run(target_col, text_run, _write_widths, cell_count, false,
                                              text_attribute{attr});
            }
            cursor.X = static_cast<SHORT>(cursor.X + cell_count);
        }

        if (cursor.X > view.Right || result.consumed < text.size())
            result.row_end = true;
        return result;
    }

    // 返回指定缓冲区坐标处 glyph 的首个 codepoint；无效坐标或空 glyph 按
    // 空格处理，匹配控制台读字符 API 的宽容语义。
    char32_t at_u32(COORD c) const noexcept
    {
        if (!_valid(c))
            return U' ';
        auto gv = row(c.Y).glyph_at(static_cast<uint16_t>(c.X));
        return gv.empty() ? U' ' : gv[0];
    }

    // 返回指定坐标处的完整内部 glyph 视图。视图引用行内 _text 或填充字符，
    // 调用者不能在修改 screen_buffer 后继续持有。
    std::u32string_view glyph_at(COORD c) const noexcept
    {
        if (!_valid(c))
            return {};
        return row(c.Y).glyph_at(static_cast<uint16_t>(c.X));
    }

    // 返回指定坐标处 glyph 的列宽；trailing 列为 0，无效坐标为 0。
    int glyph_width(COORD c) const noexcept
    {
        if (!_valid(c))
            return 0;
        return row(c.Y).glyph_width(static_cast<uint16_t>(c.X));
    }

    // ── 属性 ──
    // 返回指定列的 Win32 legacy 属性；越界返回默认 0x07，避免 API 读路径
    // 因短矩形或无效坐标产生未定义结果。
    WORD attr_at(COORD c) const noexcept
    {
        if (!_valid(c))
            return 0x07;
        return row(c.Y).attr_at(static_cast<uint16_t>(c.X)).legacy;
    }

    // 修改单个缓冲区单元的属性，不改变字符内容。无效坐标被忽略，因为
    // Console API 的短写路径会用返回长度表达实际写入范围。
    void set_attr(COORD c, WORD attr) noexcept
    {
        if (!_valid(c))
            return;
        row(c.Y).set_attr(static_cast<uint16_t>(c.X), text_attribute{attr});
    }

    // 从 start 开始填充属性。当前实现只作用于 start 所在行，用于匹配已有
    // API handler 的分段调用方式；跨行填充由 linear 版本处理。
    void fill_attrs(COORD start, ULONG count, WORD attr) noexcept
    {
        if (!_valid_y(start.Y))
            return;
        // FillConsoleOutputAttribute 在当前实现中只覆盖 start 所在行；超过行尾
        // 的部分由 row 层截断。
        auto &rr = row(start.Y);
        rr.fill_attrs(static_cast<uint16_t>(start.X), static_cast<uint16_t>(start.X + static_cast<SHORT>(count)),
                      text_attribute{attr});
    }

    // ── fill (char32_t) ──
    struct fill_result
    {
        // length_read 是请求可视为已消费的单元数；cells_modified 是实际改变的列数。
        ULONG length_read = 0;
        ULONG cells_modified = 0;
    };

    // FillConsoleOutputCharacter 语义：从 start 按线性屏幕顺序写入 count 个
    // 字符，保留每个目标单元原有属性。返回值区分请求消费量和实际改变量。
    fill_result fill_char(char32_t cp, COORD start, ULONG count)
    {
        fill_result r;
        if (!_valid_y(start.Y) || start.X < 0)
            return r;
        auto x = static_cast<SHORT>(start.X);
        auto y = static_cast<SHORT>(start.Y);
        if (x >= size.X)
            return r;

        while (count > 0 && y < size.Y)
        {
            auto &rr = row(y);
            const auto start_x = static_cast<ULONG>(x);
            const auto available = static_cast<ULONG>(size.X) - start_x;
            const auto n = count < available ? count : available;
            for (ULONG i = 0; i < n; ++i)
                rr.clear_cell(static_cast<uint16_t>(start_x + i));
            for (ULONG i = 0; i < n; ++i)
                rr.write_glyph(static_cast<uint16_t>(start_x + i), std::u32string_view{&cp, 1}, 1,
                               rr.attr_at(static_cast<uint16_t>(start_x + i)));
            r.length_read += n;
            r.cells_modified += n;
            count -= n;
            x = 0;
            ++y;
        }
        return r;
    }

    // FillConsoleOutputAttribute 语义：从 start 按线性屏幕顺序写入 count 个
    // 属性，不改变字符内容。越过缓冲区末尾时返回实际覆盖的列数。
    fill_result fill_attr(WORD attr, COORD start, ULONG count)
    {
        fill_result r;
        if (!_valid_y(start.Y) || start.X < 0)
            return r;
        auto x = static_cast<SHORT>(start.X);
        auto y = static_cast<SHORT>(start.Y);
        if (x >= size.X)
            return r;
        while (count > 0 && y < size.Y)
        {
            const auto start_x = static_cast<ULONG>(x);
            const auto available = static_cast<ULONG>(size.X) - start_x;
            const auto n = count < available ? count : available;
            row(y).fill_attrs(static_cast<uint16_t>(start_x), static_cast<uint16_t>(start_x + n), text_attribute{attr});
            r.length_read += n;
            r.cells_modified += n;
            count -= n;
            x = 0;
            ++y;
        }
        return r;
    }

    // ── write_character: char32_t 序列写入 ──
    size_t write_char32(COORD start, const char32_t *chars, size_t len)
    {
        // 返回实际写入的 char32_t 数量；可能小于 len，表示到达行尾或 Y 无效。
        if (!_valid_y(start.Y))
            return 0;
        SHORT x = start.X;
        size_t w = 0;
        auto &rr = row(start.Y);
        while (w < len && x < size.X)
        {
            // x 按显示列推进，w 按输入 codepoint 推进；双宽字符会消耗两列，
            // 但返回值仍是已消费的输入字符数。
            int cw = char_width_for_mode(chars[w], text_measurement_mode::console);
            if (cw < 1)
                cw = 1;
            if (cw > 2)
                cw = 2;
            rr.write_glyph(static_cast<uint16_t>(x), std::u32string_view{chars + w, 1}, cw,
                           rr.attr_at(static_cast<uint16_t>(x)));
            x += static_cast<SHORT>(cw);
            ++w;
        }
        return w;
    }

    // 按 Win32 线性缓冲区顺序写入字符序列：到达行尾后继续下一行。返回
    // 已消费的输入 codepoint 数；双宽字符消耗两列但只计一个输入字符。
    size_t write_char32_linear(COORD start, const char32_t *chars, size_t len)
    {
        if (!_valid(start))
            return 0;
        size_t w = 0;
        for (SHORT y = start.Y; y < size.Y && w < len; ++y)
        {
            SHORT x = (y == start.Y) ? start.X : 0;
            while (w < len && x < size.X)
            {
                int cw = char_width_for_mode(chars[w], text_measurement_mode::console);
                if (cw < 1)
                    cw = 1;
                if (cw > 2)
                    cw = 2;
                row(y).write_glyph(static_cast<uint16_t>(x), std::u32string_view{chars + w, 1}, cw,
                                   row(y).attr_at(static_cast<uint16_t>(x)));
                x += static_cast<SHORT>(cw);
                ++w;
            }
        }
        return w;
    }

    // 在单行内按列写入属性序列，不改变字符内容。返回实际写入属性数，可能
    // 因 Y 无效或行尾截断而小于 len。
    size_t write_attr_seq(COORD start, const WORD *attrs, size_t len)
    {
        // 返回实际写入的属性数量；可能小于 len，表示到达行尾或 Y 无效。
        if (!_valid_y(start.Y))
            return 0;
        SHORT x = start.X;
        size_t w = 0;
        auto &rr = row(start.Y);
        while (w < len && x < size.X)
        {
            rr.set_attr(static_cast<uint16_t>(x), text_attribute{attrs[w]});
            ++w;
            ++x;
        }
        return w;
    }

    // 按 Win32 线性缓冲区顺序写入属性序列。返回已消费属性数，可能因为
    // 起点无效或到达缓冲区末尾而小于 len。
    size_t write_attr_seq_linear(COORD start, const WORD *attrs, size_t len)
    {
        if (!_valid(start))
            return 0;
        size_t w = 0;
        for (SHORT y = start.Y; y < size.Y && w < len; ++y)
        {
            SHORT x = (y == start.Y) ? start.X : 0;
            while (w < len && x < size.X)
            {
                row(y).set_attr(static_cast<uint16_t>(x), text_attribute{attrs[w]});
                ++w;
                ++x;
            }
        }
        return w;
    }

    // ── read (wchar_t 兼容, 供 api_handlers 以 ConDrv 格式导出) ──
    size_t read_wchars(COORD start, wchar_t *out, size_t max_len) const noexcept
    {
        // 返回实际导出的 wchar_t 数量；非 BMP glyph 以 U+FFFD 代替。
        if (!_valid_y(start.Y))
            return 0;
        SHORT x = start.X;
        size_t r = 0;
        auto &rr = row(start.Y);
        while (r < max_len && x < size.X)
        {
            auto gv = rr.glyph_at(static_cast<uint16_t>(x));
            out[r] = gv.empty() ? L' ' : static_cast<wchar_t>(gv[0] <= 0xFFFF ? gv[0] : 0xFFFD);
            ++r;
            ++x;
        }
        return r;
    }

    // 按线性顺序导出 wchar_t。内部非 BMP 或多 codepoint glyph 会降级为首
    // 个可表示字符/替换字符；返回写入 out 的元素数。
    size_t read_wchars_linear(COORD start, wchar_t *out, size_t max_len) const noexcept
    {
        if (!_valid(start))
            return 0;
        size_t r = 0;
        for (SHORT y = start.Y; y < size.Y && r < max_len; ++y)
        {
            SHORT x = (y == start.Y) ? start.X : 0;
            while (x < size.X && r < max_len)
            {
                auto gv = row(y).glyph_at(static_cast<uint16_t>(x));
                out[r] = gv.empty() ? L' ' : static_cast<wchar_t>(gv[0] <= 0xFFFF ? gv[0] : 0xFFFD);
                ++r;
                ++x;
            }
        }
        return r;
    }

    // 在单行内读取属性序列。每个可见列导出一个 WORD；到达行尾或 Y 无效时
    // 停止。
    size_t read_attrs(COORD start, WORD *out, size_t max_len) const noexcept
    {
        // 返回实际导出的属性数量；可能小于 max_len。
        if (!_valid_y(start.Y))
            return 0;
        SHORT x = start.X;
        size_t r = 0;
        auto &rr = row(start.Y);
        while (r < max_len && x < size.X)
        {
            out[r] = rr.attr_at(static_cast<uint16_t>(x)).legacy;
            ++r;
            ++x;
        }
        return r;
    }

    // 按线性顺序导出属性。每个可见列导出一个 WORD，trailing 列也独立导出。
    size_t read_attrs_linear(COORD start, WORD *out, size_t max_len) const noexcept
    {
        if (!_valid(start))
            return 0;
        size_t r = 0;
        for (SHORT y = start.Y; y < size.Y && r < max_len; ++y)
        {
            SHORT x = (y == start.Y) ? start.X : 0;
            while (x < size.X && r < max_len)
            {
                out[r] = row(y).attr_at(static_cast<uint16_t>(x)).legacy;
                ++r;
                ++x;
            }
        }
        return r;
    }

    // ── clear ──
    // 清空整个缓冲区并重置环形行起点。所有行都会回到压缩空白状态，viewport
    // 尺寸和位置保持不变。
    void clear(WORD attr = 0x07)
    {
        // 0x07 是传统白前景/黑背景属性。
        _row_origin = 0;
        std::ranges::fill(_rows, screen_buffer_row(static_cast<uint16_t>(size.X), attr));
    }

    // ── scroll (纯 char32_t + WORD) ──
    // ScrollConsoleScreenBuffer 语义：把 sr 中与 clip 相交的内容移动到 dest，
    // 源区域未被目标覆盖的部分用 fill 填充。函数只修改内部屏幕模型，不发
    // VT；全屏纯垂直滚动会优先使用 _row_origin 环形移动。
    void scroll(SMALL_RECT sr, SMALL_RECT clip, bool use_clip, COORD dest, char32_t fill_char, WORD fill_attr)
    {
        COREHOST_PERF_SCOPE(screen_scroll);
        // sr 是源矩形；clip 为可写区域；dest 是源矩形左上角移动后的目标位置。
        _clamp(sr);
        if (!_rvalid(sr))
            return;
        // 没有 clip 时，整个缓冲区都是可写范围；有 clip 时，源和目标都要裁剪
        // 到 clip 内，clip 外内容保持不变。
        if (use_clip)
            _clamp(clip);
        else
            clip = {0, 0, static_cast<SHORT>(size.X - 1), static_cast<SHORT>(size.Y - 1)};

        SMALL_RECT src = sr;
        // src 是最终要复制的源区域，先取 sr 与 clip 的交集。
        if (src.Left < clip.Left)
            src.Left = clip.Left;
        if (src.Top < clip.Top)
            src.Top = clip.Top;
        if (src.Right > clip.Right)
            src.Right = clip.Right;
        if (src.Bottom > clip.Bottom)
            src.Bottom = clip.Bottom;
        if (!_rvalid(src))
        {
            _fill(sr, fill_char, fill_attr);
            return;
        }

        // dx/dy 是源矩形相对目标位置的偏移。
        SHORT dx = dest.X - sr.Left, dy = dest.Y - sr.Top;
        if (dx == 0 && dy != 0 && sr.Left == 0 && sr.Top == 0 && sr.Right == size.X - 1 && sr.Bottom == size.Y - 1 &&
            clip.Left == 0 && clip.Top == 0 && clip.Right == size.X - 1 && clip.Bottom == size.Y - 1)
        {
            const auto height = static_cast<size_t>(size.Y);
            const auto count = std::min<size_t>(dy < 0 ? -static_cast<int>(dy) : static_cast<int>(dy), height);
            if (count == height)
            {
                _row_origin = 0;
                for (SHORT y = 0; y < size.Y; ++y)
                    _fill_row(y, fill_char, fill_attr);
                return;
            }

            if (dy < 0)
            {
                _row_origin = (_row_origin + count) % height;
                for (SHORT y = static_cast<SHORT>(size.Y - count); y < size.Y; ++y)
                    _fill_row(y, fill_char, fill_attr);
            }
            else
            {
                _row_origin = (_row_origin + height - count) % height;
                for (SHORT y = 0; y < static_cast<SHORT>(count); ++y)
                    _fill_row(y, fill_char, fill_attr);
            }
            return;
        }

        _linearize_rows();
        if (dx == 0 && dy != 0 && sr.Left == 0 && sr.Right == size.X - 1 && clip.Left == sr.Left &&
            clip.Right == sr.Right && clip.Top == sr.Top && clip.Bottom == sr.Bottom)
        {
            const auto height = static_cast<int>(sr.Bottom - sr.Top + 1);
            const auto count = std::min<int>(dy < 0 ? -static_cast<int>(dy) : static_cast<int>(dy), height);
            if (dy < 0)
            {
                auto first = _rows.begin() + static_cast<std::ptrdiff_t>(sr.Top);
                auto last = _rows.begin() + static_cast<std::ptrdiff_t>(sr.Bottom + 1);
                if (count == 1)
                {
                    auto reusable = std::move(*first);
                    std::move(first + 1, last, first);
                    *(last - 1) = std::move(reusable);
                }
                else
                {
                    std::move(first + count, last, first);
                }
                for (SHORT y = static_cast<SHORT>(sr.Bottom - count + 1); y <= sr.Bottom; ++y)
                    _fill_row(y, fill_char, fill_attr);
            }
            else
            {
                auto first = _rows.begin() + static_cast<std::ptrdiff_t>(sr.Top);
                auto last = _rows.begin() + static_cast<std::ptrdiff_t>(sr.Bottom + 1);
                if (count == 1)
                {
                    auto reusable = std::move(*(last - 1));
                    std::move_backward(first, last - 1, last);
                    *first = std::move(reusable);
                }
                else
                {
                    std::move_backward(first, last - count, last);
                }
                for (SHORT y = sr.Top; y < static_cast<SHORT>(sr.Top + count); ++y)
                    _fill_row(y, fill_char, fill_attr);
            }
            return;
        }

        SMALL_RECT dst = {src.Left + dx, src.Top + dy, src.Right + dx, src.Bottom + dy};
        // 如果目标越界，反向收缩源区域，保持 src/dst 尺寸一致。
        if (dst.Left < clip.Left)
        {
            SHORT a = clip.Left - dst.Left;
            src.Left += a;
            dst.Left = clip.Left;
        }
        if (dst.Top < clip.Top)
        {
            SHORT a = clip.Top - dst.Top;
            src.Top += a;
            dst.Top = clip.Top;
        }
        if (dst.Right > clip.Right)
        {
            SHORT a = dst.Right - clip.Right;
            src.Right -= a;
            dst.Right = clip.Right;
        }
        if (dst.Bottom > clip.Bottom)
        {
            SHORT a = dst.Bottom - clip.Bottom;
            src.Bottom -= a;
            dst.Bottom = clip.Bottom;
        }

        SHORT sw = src.Right - src.Left + 1;
        SHORT sh = src.Bottom - src.Top + 1;
        if (!_rvalid(src) || sw <= 0 || sh <= 0)
        {
            _fill(sr, fill_char, fill_attr);
            return;
        }

        // saved 保存源区域内容，避免源/目标重叠时覆盖尚未复制的行。每个
        // saved row 使用完整缓冲区宽度，便于 copy_from 按列偏移写回。
        _scroll_saved_rows.resize(static_cast<size_t>(sh));
        std::ranges::for_each(std::views::iota(0, static_cast<int>(sh)), [&](int y) {
            auto &r = _rows[static_cast<size_t>(src.Top + y)];
            auto &saved = _scroll_saved_rows[static_cast<size_t>(y)];
            saved.reset_fill(static_cast<uint16_t>(size.X), U' ', text_attribute{fill_attr});
            saved.copy_from(r, static_cast<uint16_t>(src.Left), 0, static_cast<uint16_t>(sw));
        });

        SMALL_RECT fr = sr;
        // Win32 scroll 会用 fill 字符填充原源矩形与 clip 的交集，然后再写回
        // 移动后的内容；这会清掉未被目标覆盖的旧区域。
        if (fr.Left < clip.Left)
            fr.Left = clip.Left;
        if (fr.Top < clip.Top)
            fr.Top = clip.Top;
        if (fr.Right > clip.Right)
            fr.Right = clip.Right;
        if (fr.Bottom > clip.Bottom)
            fr.Bottom = clip.Bottom;
        if (_rvalid(fr))
            _fill(fr, fill_char, fill_attr);

        std::ranges::for_each(std::views::iota(0, static_cast<int>(sh)), [&](int y) {
            _rows[static_cast<size_t>(dst.Top + y)].copy_from(_scroll_saved_rows[static_cast<size_t>(y)], 0,
                                                              static_cast<uint16_t>(dst.Left),
                                                              static_cast<uint16_t>(sw));
        });
    }

    // ── CHAR_INFO 行级转换 (仅 api_handlers 使用) ──
    void row_to_ci(SHORT y, CHAR_INFO *out) const noexcept
    {
        // y 越界时不写 out；调用者通常已经准备好默认输出缓冲。
        if (y >= 0 && y < size.Y)
            row(y).to_char_info(out);
    }
    // 将 Win32 CHAR_INFO 行片段写回内部行。dst_col 是目标起始列，count 是
    // CHAR_INFO 元素数；越界 y 被忽略。
    void row_from_ci(SHORT y, const CHAR_INFO *src, uint16_t count, uint16_t dst_col = 0) noexcept
    {
        if (y >= 0 && y < size.Y)
            row(y).from_char_info(src, count, dst_col);
    }

    // ── 清除单元格为空格 (供 vt_msg_dispatch erase 使用) ──
    void clear_cell(COORD c, WORD attr = 0x07) noexcept
    {
        if (!_valid(c))
            return;
        // erase 类 VT/API 操作使用空格覆盖单元格，并设置指定属性。
        row(c.Y).clear_cell(static_cast<uint16_t>(c.X), text_attribute{attr});
    }

  private:
    // _rows.size() 必须等于 size.Y，每行宽度必须等于 size.X。逻辑行 y 通过
    // _row_origin 映射到物理行，实现全屏垂直滚动的 O(1) 行移动。
    std::vector<screen_buffer_row> _rows;

    // scroll 的临时行缓存。只在通用矩形滚动需要保存重叠源区域时使用，
    // 复用 vector 容量以减少高频滚动中的分配。
    std::vector<screen_buffer_row> _scroll_saved_rows;

    // write_text_row 的列宽缓存。只有文本段出现非单宽字符时才填充；全单宽
    // 路径保持为空，避免每段输出都保存宽度数组。
    raw_u8_buffer _write_widths;

    // 逻辑第 0 行在 _rows 中的物理下标。只有全屏纯垂直滚动会改变它；需要
    // 与外部 vector 顺序一致的操作会先调用 _linearize_rows。
    size_t _row_origin = 0;

    // 重建 _rows，使其数量等于 size.Y 且每行宽度等于 size.X。该函数不保留
    // 旧内容；需要保留内容的调用方必须先保存并复制交集。
    void _ensure_rows()
    {
        // _ensure_rows 重建所有行，不保留旧内容；需要保留内容的 resize 会在
        // 调用前先保存 old_rows 并按交集复制回来。
        _row_origin = 0;
        _rows.clear();
        _rows.reserve(static_cast<size_t>(size.Y));
        for (SHORT y = 0; y < size.Y; ++y)
            _rows.emplace_back(static_cast<uint16_t>(size.X));
    }

    // 将逻辑行号转换为 _rows 下标。y 必须是有效逻辑行；当 _rows 为空时
    // 返回 0 只用于防御默认构造早期状态。
    size_t _physical_row_index(SHORT y) const noexcept
    {
        const auto row_count = _rows.size();
        return row_count == 0 ? 0 : (_row_origin + static_cast<size_t>(y)) % row_count;
    }

    // 把环形行顺序旋转回 _rows[0] == 逻辑 0 行。resize 和通用 scroll 需要
    // 真实 vector 顺序时调用；如果 _row_origin 已经为 0 则不做工作。
    void _linearize_rows()
    {
        if (_row_origin == 0 || _rows.empty())
            return;

        std::rotate(_rows.begin(),
                    _rows.begin() + static_cast<std::vector<screen_buffer_row>::difference_type>(_row_origin),
                    _rows.end());
        _row_origin = 0;
    }

    // 判断坐标是否位于缓冲区内。无效坐标的公开读写函数通常返回默认值或
    // 短写长度，不抛异常。
    bool _valid(COORD c) const noexcept
    {
        return c.X >= 0 && c.X < size.X && c.Y >= 0 && c.Y < size.Y;
    }
    // 判断行号是否位于缓冲区内，用于只关心 Y 有效性的线性读写入口。
    bool _valid_y(SHORT y) const noexcept
    {
        return y >= 0 && y < size.Y;
    }
    // 判断矩形是否仍有面积；被 clamp 或 clip 后可能出现 Left>Right/Top>Bottom。
    bool _rvalid(const SMALL_RECT &r) const noexcept
    {
        return r.Left <= r.Right && r.Top <= r.Bottom;
    }

    // 将矩形裁剪到当前缓冲区边界。调用后仍可能无效，例如原矩形完全在边界
    // 之外，调用者需要继续用 _rvalid 判断。
    void _clamp(SMALL_RECT &r) const noexcept
    {
        if (r.Left < 0)
            r.Left = 0;
        if (r.Top < 0)
            r.Top = 0;
        if (r.Right >= size.X)
            r.Right = size.X - 1;
        if (r.Bottom >= size.Y)
            r.Bottom = size.Y - 1;
    }

    // 用指定字符和属性填充矩形区域。该路径按单元格写入，适合局部矩形；
    // 整行填充应使用 _fill_row 保留压缩状态。
    void _fill(SMALL_RECT r, char32_t cp, WORD attr)
    {
        _clamp(r);
        if (!_rvalid(r))
            return;
        for (SHORT y = r.Top; y <= r.Bottom; ++y)
            for (SHORT x = r.Left; x <= r.Right; ++x)
                row(y).write_glyph(static_cast<uint16_t>(x), std::u32string_view{&cp, 1}, 1, text_attribute{attr});
    }

    // 将逻辑行 y 重置为同一字符/属性的压缩状态，用于滚动后新露出的行。
    void _fill_row(SHORT y, char32_t cp, WORD attr)
    {
        row(y).reset_fill(static_cast<uint16_t>(size.X), cp, text_attribute{attr});
    }
};

} // namespace conpty
