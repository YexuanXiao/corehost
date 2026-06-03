#pragma once
#include <windows.h>
#include <algorithm>
#include "default_console_size.hpp"

namespace corehost::conpty
{

class console_viewport
{
  public:
    // 默认 viewport 覆盖默认控制台尺寸，坐标使用 screen buffer 绝对坐标。
    console_viewport() noexcept
    {
        reset_to_buffer(default_console_size);
    }

    // 用给定缓冲区尺寸初始化 viewport；非法尺寸会被钳制到至少 1x1。
    explicit console_viewport(COORD buffer_size) noexcept
    {
        reset_to_buffer(buffer_size);
    }

    // 返回 Win32 API 可见的窗口矩形，坐标为 screen buffer 绝对坐标。
    SMALL_RECT rect() const noexcept
    {
        return _rect;
    }

    // 返回 viewport 左上角在 screen buffer 中的绝对坐标。
    COORD origin() const noexcept
    {
        return {_rect.Left, _rect.Top};
    }

    // 返回 viewport 字符宽高；由于 _rect 始终有效，结果至少为 1x1。
    COORD size() const noexcept
    {
        return {static_cast<SHORT>(_rect.Right - _rect.Left + 1), static_cast<SHORT>(_rect.Bottom - _rect.Top + 1)};
    }

    // true 表示 viewport 覆盖整个 screen buffer，没有额外滚动区域。
    bool covers(COORD buffer_size) const noexcept
    {
        const auto width = valid_dimension(buffer_size.X);
        const auto height = valid_dimension(buffer_size.Y);
        return _rect.Left == 0 && _rect.Top == 0 && _rect.Right == width - 1 && _rect.Bottom == height - 1;
    }

    // 判断一个 screen buffer 绝对坐标是否位于当前可见 viewport 内。
    bool contains(COORD buffer_position) const noexcept
    {
        return buffer_position.X >= _rect.Left && buffer_position.X <= _rect.Right && buffer_position.Y >= _rect.Top &&
               buffer_position.Y <= _rect.Bottom;
    }

    // 把 screen buffer 绝对坐标转换成终端 viewport-relative 坐标。
    COORD relative_position(COORD buffer_position) const noexcept
    {
        return {static_cast<SHORT>(buffer_position.X - _rect.Left), static_cast<SHORT>(buffer_position.Y - _rect.Top)};
    }

    // 把终端 viewport-relative 坐标转换成 screen buffer 绝对坐标。
    COORD absolute_position(COORD terminal_position) const noexcept
    {
        return {static_cast<SHORT>(_rect.Left + terminal_position.X),
                static_cast<SHORT>(_rect.Top + terminal_position.Y)};
    }

    // 转换为 viewport-relative 坐标并钳制到可见范围内，用于生成安全 CUP。
    COORD clamped_relative_position(COORD buffer_position) const noexcept
    {
        auto terminal_position = relative_position(buffer_position);
        const auto viewport_size = size();
        terminal_position.X = std::clamp<SHORT>(terminal_position.X, 0, static_cast<SHORT>(viewport_size.X - 1));
        terminal_position.Y = std::clamp<SHORT>(terminal_position.Y, 0, static_cast<SHORT>(viewport_size.Y - 1));
        return terminal_position;
    }

    // 设置新的 viewport 矩形。rect 可以越界或尺寸非法；函数会钳制到 buffer 内。
    // 返回 true 表示最终矩形和旧矩形不同。
    bool set_rect(SMALL_RECT rect, COORD buffer_size) noexcept
    {
        const auto old = _rect;
        const auto buffer_width = valid_dimension(buffer_size.X);
        const auto buffer_height = valid_dimension(buffer_size.Y);
        const auto width = clamped_dimension(static_cast<int>(rect.Right) - rect.Left + 1, buffer_width);
        const auto height = clamped_dimension(static_cast<int>(rect.Bottom) - rect.Top + 1, buffer_height);
        const auto left = std::clamp<SHORT>(rect.Left, 0, static_cast<SHORT>(buffer_width - width));
        const auto top = std::clamp<SHORT>(rect.Top, 0, static_cast<SHORT>(buffer_height - height));
        _rect = {left, top, static_cast<SHORT>(left + width - 1), static_cast<SHORT>(top + height - 1)};
        return old.Left != _rect.Left || old.Top != _rect.Top || old.Right != _rect.Right || old.Bottom != _rect.Bottom;
    }

    // 保持当前 origin，调整 viewport 尺寸；返回 true 表示最终矩形发生变化。
    bool set_size(COORD viewport_size, COORD buffer_size) noexcept
    {
        SMALL_RECT next = _rect;
        next.Right = static_cast<SHORT>(next.Left + std::max<SHORT>(viewport_size.X, 1) - 1);
        next.Bottom = static_cast<SHORT>(next.Top + std::max<SHORT>(viewport_size.Y, 1) - 1);
        return set_rect(next, buffer_size);
    }

    // 保持当前尺寸，移动 viewport origin；返回 true 表示最终矩形发生变化。
    bool set_origin(COORD origin, COORD buffer_size) noexcept
    {
        const auto viewport_size = size();
        return set_rect({origin.X, origin.Y, static_cast<SHORT>(origin.X + viewport_size.X - 1),
                         static_cast<SHORT>(origin.Y + viewport_size.Y - 1)},
                        buffer_size);
    }

    // 按 delta 移动 viewport origin；delta 可为负，最终位置仍钳制到 buffer 内。
    bool move_origin(COORD delta, COORD buffer_size) noexcept
    {
        return set_origin({static_cast<SHORT>(_rect.Left + delta.X), static_cast<SHORT>(_rect.Top + delta.Y)},
                          buffer_size);
    }

    // 缓冲区尺寸改变后重新钳制现有 viewport；返回 true 表示 viewport 被移动或缩放。
    bool clamp_to_buffer(COORD buffer_size) noexcept
    {
        return set_rect(_rect, buffer_size);
    }

    // 确保 cursor_position 进入可见 viewport；必要时只移动 origin，不改变尺寸。
    bool snap_to_cursor(COORD cursor_position, COORD buffer_size) noexcept
    {
        auto next_origin = origin();
        if (cursor_position.X < _rect.Left)
            next_origin.X = cursor_position.X;
        else if (cursor_position.X > _rect.Right)
            next_origin.X = static_cast<SHORT>(next_origin.X + cursor_position.X - _rect.Right);

        if (cursor_position.Y < _rect.Top)
            next_origin.Y = cursor_position.Y;
        else if (cursor_position.Y > _rect.Bottom)
            next_origin.Y = static_cast<SHORT>(next_origin.Y + cursor_position.Y - _rect.Bottom);

        return set_origin(next_origin, buffer_size);
    }

    // 让 viewport 覆盖整个 buffer；用于初始化或全屏清空后重置滚动窗口。
    void reset_to_buffer(COORD buffer_size) noexcept
    {
        const auto width = valid_dimension(buffer_size.X);
        const auto height = valid_dimension(buffer_size.Y);
        _rect = {0, 0, static_cast<SHORT>(width - 1), static_cast<SHORT>(height - 1)};
    }

  private:
    SMALL_RECT _rect{};

    // 把 Win32 API 可能传入的 0/负尺寸修正为可建模的最小尺寸。
    static SHORT valid_dimension(SHORT value) noexcept
    {
        return std::max<SHORT>(value, 1);
    }

    // 把请求宽高限制在 1..maximum，供 set_rect 处理异常窗口尺寸。
    static SHORT clamped_dimension(int value, SHORT maximum) noexcept
    {
        return static_cast<SHORT>(std::clamp(value, 1, static_cast<int>(maximum)));
    }
};

} // namespace corehost::conpty
