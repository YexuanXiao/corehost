// ── conpty/screen_buffer.hpp ──────────────────────
// 屏幕缓冲区 — 纯 char32_t 内部存储, 对标 terminal/src/buffer/out/TextBuffer
//
// 内部: screen_buffer_row (u32string _text + column offsets + text_attribute)
// CHAR_INFO 仅在 api_handlers 通过 row_to_char_info/row_from_char_info 转换,
// 不进入 screen_buffer 的其他公开接口。
//
// grapheme cluster 支持: _columns offset + 0x8000 trailing flag
#pragma once
#include <windows.h>
#include <cstring>
#include <vector>
#include "default_console_size.hpp"
#include "screen_buffer_row.hpp"
#include "char_convert.hpp"
#include "char_width.hpp"

namespace conpty
{

struct screen_buffer
{
    COORD size{default_console_size};

    screen_buffer()
    {
        _ensure_rows();
    }
    explicit screen_buffer(COORD sz) : size(sz)
    {
        _ensure_rows();
    }

    void resize(COORD new_size)
    {
        if (new_size.X < 1)
            new_size.X = 1;
        if (new_size.Y < 1)
            new_size.Y = 1;
        if (new_size.X == size.X && new_size.Y == size.Y)
            return;

        auto old_rows = std::move(_rows);
        auto old_size = size;
        size = new_size;
        _ensure_rows();

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
        auto &rr = row(start.Y);
        rr.fill_attrs(static_cast<uint16_t>(start.X), static_cast<uint16_t>(start.X + static_cast<SHORT>(count)),
                      text_attribute{attr});
    }

    // ── fill (char32_t) ──
    struct fill_result
    {
        ULONG length_read = 0;
        ULONG cells_modified = 0;
    };

    fill_result fill_char(char32_t cp, COORD start, ULONG count)
    {
        fill_result r;
        if (!_valid_y(start.Y))
            return r;
        ULONG rs = static_cast<ULONG>(start.X);
        if (rs >= static_cast<ULONG>(size.X))
            return r;
        ULONG rem = static_cast<ULONG>(size.X) - rs;
        ULONG n = count < rem ? count : rem;
        auto &rr = row(start.Y);
        for (ULONG i = 0; i < n; ++i)
            rr.clear_cell(static_cast<uint16_t>(rs + i));
        for (ULONG i = 0; i < n; ++i)
            rr.write_glyph(static_cast<uint16_t>(rs + i), std::u32string_view{&cp, 1}, 1,
                           rr.attr_at(static_cast<uint16_t>(rs + i)));
        r.length_read = r.cells_modified = n;
        return r;
    }

    fill_result fill_attr(WORD attr, COORD start, ULONG count)
    {
        fill_result r;
        if (!_valid_y(start.Y))
            return r;
        ULONG rs = static_cast<ULONG>(start.X);
        if (rs >= static_cast<ULONG>(size.X))
            return r;
        ULONG rem = static_cast<ULONG>(size.X) - rs;
        ULONG n = count < rem ? count : rem;
        row(start.Y).fill_attrs(static_cast<uint16_t>(rs), static_cast<uint16_t>(rs + n), text_attribute{attr});
        r.length_read = r.cells_modified = n;
        return r;
    }

    // ── write_character: char32_t 序列写入 ──
    size_t write_char32(COORD start, const char32_t *chars, size_t len)
    {
        if (!_valid_y(start.Y))
            return 0;
        SHORT x = start.X;
        size_t w = 0;
        auto &rr = row(start.Y);
        while (w < len && x < size.X)
        {
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

    size_t write_attr_seq(COORD start, const WORD *attrs, size_t len)
    {
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

    // ── read (wchar_t 兼容, 供 api_handlers 以 ConDrv 格式导出) ──
    size_t read_wchars(COORD start, wchar_t *out, size_t max_len) const noexcept
    {
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

    size_t read_attrs(COORD start, WORD *out, size_t max_len) const noexcept
    {
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

    // ── clear ──
    void clear(WORD attr = 0x07)
    {
        for (auto &r : _rows)
            r = screen_buffer_row(static_cast<uint16_t>(size.X), attr);
    }

    // ── scroll (纯 char32_t + WORD) ──
    void scroll(SMALL_RECT sr, SMALL_RECT clip, bool use_clip, COORD dest, char32_t fill_char, WORD fill_attr)
    {
        _clamp(sr);
        if (!_rvalid(sr))
            return;
        if (use_clip)
            _clamp(clip);
        else
            clip = {0, 0, static_cast<SHORT>(size.X - 1), static_cast<SHORT>(size.Y - 1)};

        SMALL_RECT src = sr;
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

        SHORT dx = dest.X - sr.Left, dy = dest.Y - sr.Top;
        SMALL_RECT dst = {src.Left + dx, src.Top + dy, src.Right + dx, src.Bottom + dy};
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

        std::vector<screen_buffer_row> saved(sh);
        for (SHORT y = 0; y < sh; ++y)
        {
            auto &r = _rows[static_cast<size_t>(src.Top + y)];
            saved[y] = screen_buffer_row(static_cast<uint16_t>(size.X), fill_attr);
            saved[y].copy_from(r, static_cast<uint16_t>(src.Left), 0, static_cast<uint16_t>(sw));
        }

        SMALL_RECT fr = sr;
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
        row(c.Y).clear_cell(static_cast<uint16_t>(c.X), text_attribute{attr});
    }

  private:
    std::vector<screen_buffer_row> _rows;

    void _ensure_rows()
    {
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
                row(y).clear_cell(static_cast<uint16_t>(x), text_attribute{attr});
    }
};

} // namespace conpty
