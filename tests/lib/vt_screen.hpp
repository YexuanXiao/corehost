// ── tests/lib/vt_screen.hpp ───────────────────────────────
// 测试用的最小 VT 屏幕模型：把 ConPTY 输出喂进来，断言"渲染后的屏幕内容"
// 而不是字节子串。覆盖常用子集：可打印文本/换行/回车/退格/TAB、CUP/CUU/CUD/
// CUF/CUB、ED/EL、SGR 前景/背景/反显/加粗、主/备用缓冲区（1049）。
//
// 不追求完整 VT 语义，也不实现 scrollback：行数固定，越界滚动丢弃顶部；
// 只服务于测试断言（单元格/文本/光标/属性）。
#pragma once
#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <vector>

namespace corehost::test
{

struct screen_cell
{
    char32_t ch = U' ';
    uint8_t fg = 7; // 0..15
    uint8_t bg = 0; // 0..15
    bool bold = false;
    bool reverse = false;

    bool operator==(const screen_cell &) const = default;
};

class vt_screen
{
  public:
    explicit vt_screen(short width = 80, short height = 25)
        : _width{width}, _height{height}, _grid(static_cast<size_t>(width) * static_cast<size_t>(height)),
          _alt(static_cast<size_t>(width) * static_cast<size_t>(height))
    {
    }

    void feed(std::string_view bytes)
    {
        for (const char raw : bytes)
            feed_byte(static_cast<unsigned char>(raw));
    }

    [[nodiscard]] short width() const noexcept
    {
        return _width;
    }

    [[nodiscard]] short height() const noexcept
    {
        return _height;
    }

    [[nodiscard]] COORD cursor() const noexcept
    {
        return COORD{_x, _y};
    }

    [[nodiscard]] const screen_cell &cell(short x, short y) const noexcept
    {
        static const screen_cell blank{};
        if (x < 0 || y < 0 || x >= _width || y >= _height)
            return blank;
        return grid()[static_cast<size_t>(y) * static_cast<size_t>(_width) + static_cast<size_t>(x)];
    }

    // 一行的可见文本（去掉行尾空格）。
    [[nodiscard]] std::string text(short y) const
    {
        std::string result;
        for (short x = 0; x < _width; ++x)
        {
            const char32_t ch = cell(x, y).ch;
            if (ch == U'\0')
                break;
            append_utf8(result, ch);
        }
        while (!result.empty() && result.back() == ' ')
            result.pop_back();
        return result;
    }

    [[nodiscard]] bool contains(std::string_view needle) const
    {
        for (short y = 0; y < _height; ++y)
        {
            if (text(y).find(needle) != std::string::npos)
                return true;
        }
        return false;
    }

    // 返回第一个包含 needle 的位置（0-based）；找不到返回 {-1,-1}。
    [[nodiscard]] COORD find(std::string_view needle) const
    {
        for (short y = 0; y < _height; ++y)
        {
            const auto row = text(y);
            const auto position = row.find(needle);
            if (position != std::string::npos)
                return COORD{static_cast<SHORT>(position), y};
        }
        return COORD{-1, -1};
    }

  private:
    [[nodiscard]] std::vector<screen_cell> &grid() noexcept
    {
        return _use_alt ? _alt : _grid;
    }

    [[nodiscard]] const std::vector<screen_cell> &grid() const noexcept
    {
        return _use_alt ? _alt : _grid;
    }

    void append_utf8(std::string &out, char32_t ch) const
    {
        if (ch <= 0x7F)
        {
            out.push_back(static_cast<char>(ch));
        }
        else if (ch <= 0x7FF)
        {
            out.push_back(static_cast<char>(0xC0 | (ch >> 6)));
            out.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
        }
        else if (ch <= 0xFFFF)
        {
            out.push_back(static_cast<char>(0xE0 | (ch >> 12)));
            out.push_back(static_cast<char>(0x80 | ((ch >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
        }
        else
        {
            out.push_back(static_cast<char>(0xF0 | (ch >> 18)));
            out.push_back(static_cast<char>(0x80 | ((ch >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((ch >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
        }
    }

    void feed_byte(unsigned char byte)
    {
        switch (_state)
        {
        case state::ground:
            feed_ground(byte);
            break;
        case state::escape:
            feed_escape(byte);
            break;
        case state::csi:
            feed_csi(byte);
            break;
        case state::osc:
            if (byte == 0x07)
                _state = state::ground;
            else if (byte == 0x1B)
                _state = state::osc_st;
            break;
        case state::osc_st:
            _state = byte == '\\' ? state::ground : state::osc;
            break;
        }
    }

    void feed_ground(unsigned char byte)
    {
        if (byte == 0x1B)
        {
            _state = state::escape;
            return;
        }
        if (_pending_utf8 != 0)
        {
            if ((byte & 0xC0) == 0x80)
            {
                _codepoint = (_codepoint << 6) | (byte & 0x3F);
                if (--_pending_utf8 == 0)
                    put_char(_codepoint);
                return;
            }
            _pending_utf8 = 0; // 非法的续字节，丢弃半截序列
        }

        switch (byte)
        {
        case '\r':
            _x = 0;
            break;
        case '\n':
            line_feed();
            break;
        case '\b':
            if (_x > 0)
                --_x;
            break;
        case '\t':
            _x = static_cast<short>(std::min<int>(_width - 1, (_x / 8 + 1) * 8));
            break;
        case 0x07:
            break; // bell
        default:
            if (byte < 0x20)
                return;
            if (byte < 0x80)
            {
                put_char(byte);
            }
            else if ((byte & 0xE0) == 0xC0)
            {
                _codepoint = byte & 0x1F;
                _pending_utf8 = 1;
            }
            else if ((byte & 0xF0) == 0xE0)
            {
                _codepoint = byte & 0x0F;
                _pending_utf8 = 2;
            }
            else if ((byte & 0xF8) == 0xF0)
            {
                _codepoint = byte & 0x07;
                _pending_utf8 = 3;
            }
            break;
        }
    }

    void feed_escape(unsigned char byte)
    {
        switch (byte)
        {
        case '[':
            _params.clear();
            _current_param = 0;
            _has_param = false;
            _private = 0;
            _state = state::csi;
            break;
        case ']':
            _state = state::osc;
            break;
        case '(':
        case ')':
            // 字符集选择，忽略
            _state = state::escape;
            break;
        case '7':
        case '8':
            _state = state::ground;
            break;
        case '\\':
            _state = state::ground;
            break;
        default:
            _state = byte == 0x1B ? state::escape : state::ground;
            break;
        }
    }

    void feed_csi(unsigned char byte)
    {
        if (byte >= '0' && byte <= '9')
        {
            _current_param = static_cast<int>(_current_param * 10 + (byte - '0'));
            _has_param = true;
            return;
        }
        if (byte == ';')
        {
            _params.push_back(_current_param);
            _current_param = 0;
            _has_param = false;
            return;
        }
        if (byte >= 0x3C && byte <= 0x3F)
        {
            _private = static_cast<char>(byte);
            return;
        }
        if (byte == '?')
        {
            _private = '?';
            return;
        }
        if (byte >= 0x20 && byte <= 0x2F)
            return; // 忽略 intermediate

        if (_has_param || !_params.empty())
            _params.push_back(_current_param);
        csi_dispatch(static_cast<char>(byte));
        _state = state::ground;
    }

    [[nodiscard]] int param(size_t index, int fallback = 0) const
    {
        if (index >= _params.size())
            return fallback;
        const int value = _params[index];
        return value == 0 ? fallback : value;
    }

    void csi_dispatch(char final)
    {
        switch (final)
        {
        case 'H':
        case 'f':
            // CSI row;col H
            _y = static_cast<short>(std::clamp(param(0, 1) - 1, 0, _height - 1));
            _x = static_cast<short>(std::clamp(param(1, 1) - 1, 0, _width - 1));
            break;
        case 'A':
            _y = static_cast<short>(std::max(0, _y - param(0, 1)));
            break;
        case 'B':
            _y = static_cast<short>(std::min<int>(_height - 1, _y + param(0, 1)));
            break;
        case 'C':
            _x = static_cast<short>(std::min<int>(_width - 1, _x + param(0, 1)));
            break;
        case 'D':
            _x = static_cast<short>(std::max(0, _x - param(0, 1)));
            break;
        case 'G':
            _x = static_cast<short>(std::clamp(param(0, 1) - 1, 0, _width - 1));
            break;
        case 'd':
            _y = static_cast<short>(std::clamp(param(0, 1) - 1, 0, _height - 1));
            break;
        case 'J':
            erase_display(param(0, 0));
            break;
        case 'K':
            erase_line(param(0, 0));
            break;
        case 'm':
            apply_sgr();
            break;
        case 'h':
        case 'l':
            set_private_modes(final == 'h');
            break;
        default:
            break;
        }
    }

    void set_private_modes(bool enabled)
    {
        if (_private != '?')
            return;
        for (const int mode : _params)
        {
            if (mode == 1049)
            {
                if (enabled && !_use_alt)
                {
                    // conhost 的备用缓冲区初始为空白；主缓冲区保持不变。
                    _alt.assign(_alt.size(), screen_cell{});
                    _use_alt = true;
                    _x = 0;
                    _y = 0;
                }
                else if (!enabled && _use_alt)
                {
                    _use_alt = false;
                    _x = 0;
                    _y = 0;
                }
            }
        }
    }

    void apply_sgr()
    {
        if (_params.empty())
            _params.push_back(0);
        for (const int code : _params)
        {
            switch (code)
            {
            case 0:
                _fg = 7;
                _bg = 0;
                _bold = false;
                _reverse = false;
                break;
            case 1:
                _bold = true;
                break;
            case 7:
                _reverse = true;
                break;
            case 27:
                _reverse = false;
                break;
            case 22:
                _bold = false;
                break;
            case 39:
                _fg = 7;
                break;
            case 49:
                _bg = 0;
                break;
            default:
                if (code >= 30 && code <= 37)
                    _fg = static_cast<uint8_t>(code - 30);
                else if (code >= 40 && code <= 47)
                    _bg = static_cast<uint8_t>(code - 40);
                else if (code >= 90 && code <= 97)
                    _fg = static_cast<uint8_t>(code - 90 + 8);
                else if (code >= 100 && code <= 107)
                    _bg = static_cast<uint8_t>(code - 100 + 8);
                break;
            }
        }
    }

    void erase_line(int mode)
    {
        for (short x = 0; x < _width; ++x)
        {
            const bool clear = mode == 0 ? x >= _x : (mode == 1 ? x <= _x : true);
            if (clear)
                set_cell(x, _y, U' ', false);
        }
    }

    void erase_display(int mode)
    {
        if (mode == 2 || mode == 3)
        {
            for (auto &cell_value : grid())
                cell_value = screen_cell{};
            return;
        }
        erase_line(mode == 1 ? 1 : 0);
        if (mode == 1)
        {
            for (short y = 0; y < _y; ++y)
                for (short x = 0; x < _width; ++x)
                    set_cell(x, y, U' ', false);
        }
        else
        {
            for (short y = static_cast<short>(_y + 1); y < _height; ++y)
                for (short x = 0; x < _width; ++x)
                    set_cell(x, y, U' ', false);
        }
    }

    void set_cell(short x, short y, char32_t ch, bool wrap_pending)
    {
        auto &target = grid()[static_cast<size_t>(y) * static_cast<size_t>(_width) + static_cast<size_t>(x)];
        target.ch = ch;
        target.fg = wrap_pending ? _fg : _fg;
        target.bg = _bg;
        target.bold = _bold;
        target.reverse = _reverse;
    }

    void put_char(char32_t ch)
    {
        if (_x >= _width)
        {
            _x = 0;
            line_feed();
        }
        set_cell(_x, _y, ch, false);
        if (_x < _width - 1)
            ++_x;
        else
            _x = _width; // 下一字符触发换行
    }

    void line_feed()
    {
        if (_y < _height - 1)
        {
            ++_y;
            return;
        }
        // 滚屏：丢弃第一行。
        auto &cells = grid();
        const auto row_cells = static_cast<size_t>(_width);
        std::move(cells.begin() + static_cast<std::ptrdiff_t>(row_cells), cells.end(), cells.begin());
        std::fill(cells.begin() + static_cast<std::ptrdiff_t>(cells.size() - row_cells), cells.end(), screen_cell{});
    }

    enum class state
    {
        ground,
        escape,
        csi,
        osc,
        osc_st,
    };

    short _width = 80;
    short _height = 25;
    std::vector<screen_cell> _grid;
    std::vector<screen_cell> _alt;
    bool _use_alt = false;
    short _x = 0;
    short _y = 0;
    state _state = state::ground;
    std::vector<int> _params;
    int _current_param = 0;
    bool _has_param = false;
    char _private = 0;
    char32_t _codepoint = 0;
    int _pending_utf8 = 0;
    uint8_t _fg = 7;
    uint8_t _bg = 0;
    bool _bold = false;
    bool _reverse = false;
};

} // namespace corehost::test
