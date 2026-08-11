#pragma once
#include <windows.h>
#include <cstddef>

namespace corehost::conpty
{

class terminal_cursor_state
{
  public:
    // 返回 corehost 当前相信的终端相对坐标；只有 cursor_valid()==true 时
    // 这个值才可用于生成 CUP 或同步 console_state。
    COORD cursor() const noexcept
    {
        return _cursor;
    }

    // false 表示尚未通过首个输出或 CPR 继承得到可信终端坐标。
    bool cursor_valid() const noexcept
    {
        return _cursor_valid;
    }

    // 设置一个新的可信终端坐标；调用方已经完成 viewport-relative 转换。
    void set_cursor(COORD cursor) noexcept
    {
        _cursor = cursor;
        _cursor_valid = true;
    }

    // 本地 echo 输出一个普通可见字符后调用，同时扩大当前输入行右边界。
    void advance() noexcept
    {
        if (!_cursor_valid)
            return;
        ++_cursor.X;
        if (_cursor.X > _input_column_end)
            _input_column_end = _cursor.X;
    }

    // 本地编辑回退一列；不会越过 prompt 后建立的输入行左边界。
    void retreat() noexcept
    {
        if (!_cursor_valid)
            return;
        if (_cursor.X > _input_column_start)
            --_cursor.X;
    }

    // 终端已经执行 CRLF 后调用；用于把后续 API 输出定位到下一行行首。
    void crlf() noexcept
    {
        if (!_cursor_valid)
            return;
        _cursor.X = 0;
        ++_cursor.Y;
    }

    // 终端已经执行 CR 后调用；只更新列，不改变行。
    void carriage_return() noexcept
    {
        if (_cursor_valid)
            _cursor.X = 0;
    }

    // 终端输入流中的 LF 在当前实现中按 CRLF 处理，保持 shell prompt 对齐。
    void line_feed() noexcept
    {
        if (!_cursor_valid)
            return;
        _cursor.X = 0;
        ++_cursor.Y;
    }

    // 把 cooked buffer 中的字符偏移换算为当前输入行的终端列。
    SHORT column_for_offset(size_t offset) const noexcept
    {
        return static_cast<SHORT>(_input_column_start + offset);
    }

    // 设置光标列但要求已有可信坐标；用于普通编辑移动。
    void set_cursor_x(SHORT x) noexcept
    {
        if (_cursor_valid)
            _cursor.X = x;
    }

    // 设置光标列且不检查坐标可信度；只给已经手动维护状态的测试/内部路径使用。
    void set_cursor_x_unchecked(SHORT x) noexcept
    {
        _cursor.X = x;
    }

    // 新 prompt 或 Console API 输出结束后重建可编辑输入区间。
    void reset_bounds(SHORT x) noexcept
    {
        _input_column_start = x;
        _input_column_end = x;
    }

    // 插入一个字符后扩大可编辑区间右边界。
    void extend_bounds() noexcept
    {
        ++_input_column_end;
    }

    // 删除一个字符后收缩可编辑区间右边界，最低保持在 prompt 后的位置。
    void retract_bounds() noexcept
    {
        if (_input_column_end > _input_column_start)
            --_input_column_end;
    }

    // 根据 cooked buffer 长度一次性设置输入区间右边界，用于历史行替换。
    void set_bounds_end_for_length(size_t length) noexcept
    {
        _input_column_end = static_cast<SHORT>(_input_column_start + length);
    }

    // 返回本轮 cooked input 允许移动到的最左列。
    SHORT input_column_start() const noexcept
    {
        return _input_column_start;
    }

    // 返回本轮 cooked input 当前占用到的最右后一列。
    SHORT input_column_end() const noexcept
    {
        return _input_column_end;
    }

    // 测试或特殊重放路径直接修正输入左边界。
    void set_input_column_start(SHORT x) noexcept
    {
        _input_column_start = x;
    }

    // 测试或特殊重放路径直接修正输入右边界。
    void set_input_column_end(SHORT x) noexcept
    {
        _input_column_end = x;
    }

    // 取走 Enter 后延迟 CUP 标记；返回 true 表示调用方需要先移动到 enter_dest。
    bool consume_enter_newline() noexcept
    {
        if (!_enter_pending_newline)
            return false;
        _enter_pending_newline = false;
        return true;
    }

    // 查询下一次输出前是否需要执行 Enter 后的行首定位修正。
    bool enter_newline_pending() const noexcept
    {
        return _enter_pending_newline;
    }

    // 测试或恢复路径直接设置 Enter 后行首定位标记。
    void set_enter_newline_pending(bool pending) noexcept
    {
        _enter_pending_newline = pending;
    }

    // 返回 Enter 本地 echo 后的目标位置；只有 enter_newline_pending()==true
    // 时调用它才有语义。
    COORD enter_dest() const noexcept
    {
        return _enter_dest;
    }

    // 清除 Enter 后定位修正，供 clear/prompt 重绘等已经自行定位的路径使用。
    void reset_enter_newline() noexcept
    {
        _enter_pending_newline = false;
    }

    // 记录当前终端坐标为下一次 Console API 输出前应恢复的位置。
    // 光标未继承时忽略：enter_dest 依赖可信光标，避免无 CPR 场景产生
    // 错误的换行定位。
    void mark_enter_newline_at_cursor() noexcept
    {
        if (!_cursor_valid)
            return;
        _enter_dest = _cursor;
        _enter_pending_newline = true;
    }

    // 启动 CPR 继承流程；下一条 CPR 响应会初始化终端光标。
    void set_pending_inherit_cursor() noexcept
    {
        _pending_inherit_cursor = true;
    }

    // true 表示当前正在等待 WT 对 DSR CPR 的光标位置响应。
    bool pending_inherit_cursor() const noexcept
    {
        return _pending_inherit_cursor;
    }

    // 用 CPR 报告的坐标完成继承，并以该列作为新的输入边界。
    void finish_inherit_cursor(COORD cursor) noexcept
    {
        set_cursor(cursor);
        reset_bounds(cursor.X);
        _pending_inherit_cursor = false;
    }

    // 用实际回显到终端的原始字节推进终端坐标；只处理能影响列/行的
    // ASCII 控制字节，复杂 VT 序列由 parser/bridge 的高层路径处理。
    void apply_echo_byte(BYTE byte) noexcept
    {
        if (!_cursor_valid)
            return;
        if (byte == '\r')
        {
            _cursor.X = 0;
        }
        else if (byte == '\n')
        {
            _cursor.X = 0;
            ++_cursor.Y;
        }
        else if (byte == 0x08 || byte == 0x7F)
        {
            if (_cursor.X > _input_column_start)
                --_cursor.X;
            if (_cursor.X < _input_column_end)
                _input_column_end = _cursor.X;
        }
        else if (byte >= 0x20)
        {
            ++_cursor.X;
            if (_cursor.X > _input_column_end)
                _input_column_end = _cursor.X;
        }
    }

  private:
    // 宿主终端 viewport-relative 光标。只有 _cursor_valid 为 true 时可信；
    // 初始 (0,0) 只是占位值。
    COORD _cursor{0, 0};
    // false 表示尚未继承或建立终端光标，行编辑只能维护 cooked 状态，不能发
    // 依赖当前坐标的相对重绘。
    bool _cursor_valid = false;
    // true 表示 Enter 已经本地 echo CRLF，下一次 Console API 输出前需要
    // 先 CUP 到 _enter_dest。
    bool _enter_pending_newline = false;
    // Enter 本地 echo 后的目标行首坐标；仅在 _enter_pending_newline 为 true
    // 时由 pipe_bridge::get_enter_dest 读取。
    COORD _enter_dest{0, 0};
    // true 表示已经发送 DSR CPR，下一条 CPR 响应用来初始化 _cursor。
    bool _pending_inherit_cursor = false;
    // 当前 cooked input 可编辑区域的起始列，通常是 prompt 后的位置。
    SHORT _input_column_start = 0;
    // 当前 cooked input 已显示文本的右边界后一列。
    SHORT _input_column_end = 0;
};

} // namespace corehost::conpty
