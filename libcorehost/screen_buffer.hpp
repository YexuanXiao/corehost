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
#include <cstring>
#include <vector>
#include "default_console_size.hpp"
#include "console_viewport.hpp"
#include "screen_buffer_row.hpp"
#include "char_convert.hpp"
#include "char_width.hpp"

namespace conpty
{

struct screen_buffer
{
    // 字符列/行数，必须保持 >= 1。resize 会修正非法输入。
    COORD size{default_console_size};
    console_viewport viewport{default_console_size};

    screen_buffer()
    {
        _ensure_rows();
    }
    explicit screen_buffer(COORD sz) : size(sz)
                                      , viewport(sz)
    {
        _ensure_rows();
    }

    void resize(COORD new_size)
    {
        // 控制台缓冲区不能为 0 行或 0 列。
        if (new_size.X < 1)
            new_size.X = 1;
        if (new_size.Y < 1)
            new_size.Y = 1;
        if (new_size.X == size.X && new_size.Y == size.Y)
            return;

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
    screen_buffer_row &row(SHORT y) noexcept
    {
        return _rows[static_cast<size_t>(y)];
    }
    const screen_buffer_row &row(SHORT y) const noexcept
    {
        return _rows[static_cast<size_t>(y)];
    }

    // ── 单 glyph 读写 (char32_t) ──
    void set_u32(COORD c, char32_t cp, WORD attr = 0x07)
    {
        if (!_valid(c))
            return;
        // Console 模式只区分 1/2 列宽；组合字符和控制字符在这里至少占一列，
        // 防止屏幕模型出现 0 宽单元格。
        int cw = char_width_for_mode(cp, text_measurement_mode::console);
        if (cw < 1)
            cw = 1;
        if (cw > 2)
            cw = 2;
        row(c.Y).write_glyph(static_cast<uint16_t>(c.X), std::u32string_view{&cp, 1}, cw, text_attribute{attr});
    }

    char32_t at_u32(COORD c) const noexcept
    {
        if (!_valid(c))
            return U' ';
        auto gv = row(c.Y).glyph_at(static_cast<uint16_t>(c.X));
        return gv.empty() ? U' ' : gv[0];
    }

    std::u32string_view glyph_at(COORD c) const noexcept
    {
        if (!_valid(c))
            return {};
        return row(c.Y).glyph_at(static_cast<uint16_t>(c.X));
    }

    int glyph_width(COORD c) const noexcept
    {
        if (!_valid(c))
            return 0;
        return row(c.Y).glyph_width(static_cast<uint16_t>(c.X));
    }

    // ── 属性 ──
    WORD attr_at(COORD c) const noexcept
    {
        if (!_valid(c))
            return 0x07;
        return row(c.Y).attr_at(static_cast<uint16_t>(c.X)).legacy;
    }

    void set_attr(COORD c, WORD attr) noexcept
    {
        if (!_valid(c))
            return;
        row(c.Y).set_attr(static_cast<uint16_t>(c.X), text_attribute{attr});
    }

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
            row(y).fill_attrs(static_cast<uint16_t>(start_x), static_cast<uint16_t>(start_x + n),
                              text_attribute{attr});
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
    void clear(WORD attr = 0x07)
    {
        // 0x07 是传统白前景/黑背景属性。
        for (auto &r : _rows)
            r = screen_buffer_row(static_cast<uint16_t>(size.X), attr);
    }

    // ── scroll (纯 char32_t + WORD) ──
    void scroll(SMALL_RECT sr, SMALL_RECT clip, bool use_clip, COORD dest, char32_t fill_char, WORD fill_attr)
    {
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
        std::vector<screen_buffer_row> saved(sh);
        for (SHORT y = 0; y < sh; ++y)
        {
            auto &r = _rows[static_cast<size_t>(src.Top + y)];
            saved[y] = screen_buffer_row(static_cast<uint16_t>(size.X), fill_attr);
            saved[y].copy_from(r, static_cast<uint16_t>(src.Left), 0, static_cast<uint16_t>(sw));
        }

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

        for (SHORT y = 0; y < sh; ++y)
            _rows[static_cast<size_t>(dst.Top + y)].copy_from(saved[y], 0, static_cast<uint16_t>(dst.Left),
                                                              static_cast<uint16_t>(sw));
    }

    // ── CHAR_INFO 行级转换 (仅 api_handlers 使用) ──
    void row_to_ci(SHORT y, CHAR_INFO *out) const noexcept
    {
        // y 越界时不写 out；调用者通常已经准备好默认输出缓冲。
        if (y >= 0 && y < size.Y)
            row(y).to_char_info(out);
    }
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
    // _rows.size() 必须等于 size.Y，每行宽度必须等于 size.X。
    std::vector<screen_buffer_row> _rows;

    void _ensure_rows()
    {
        // _ensure_rows 重建所有行，不保留旧内容；需要保留内容的 resize 会在
        // 调用前先保存 old_rows 并按交集复制回来。
        _rows.clear();
        _rows.reserve(static_cast<size_t>(size.Y));
        for (SHORT y = 0; y < size.Y; ++y)
            _rows.emplace_back(static_cast<uint16_t>(size.X));
    }

    bool _valid(COORD c) const noexcept
    {
        return c.X >= 0 && c.X < size.X && c.Y >= 0 && c.Y < size.Y;
    }
    bool _valid_y(SHORT y) const noexcept
    {
        return y >= 0 && y < size.Y;
    }
    bool _rvalid(const SMALL_RECT &r) const noexcept
    {
        return r.Left <= r.Right && r.Top <= r.Bottom;
    }

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

    void _fill(SMALL_RECT r, char32_t cp, WORD attr)
    {
        _clamp(r);
        if (!_rvalid(r))
            return;
        for (SHORT y = r.Top; y <= r.Bottom; ++y)
            for (SHORT x = r.Left; x <= r.Right; ++x)
            {
                row(y).clear_cell(static_cast<uint16_t>(x), text_attribute{attr});
                row(y).write_glyph(static_cast<uint16_t>(x), std::u32string_view{&cp, 1}, 1,
                                   text_attribute{attr});
            }
    }
};

} // namespace conpty
