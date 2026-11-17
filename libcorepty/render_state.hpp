#pragma once

#include "console_state.hpp"
#include "control.hpp"
#include "screen_buffer.hpp"
#include "viewport.hpp"

#include <cstddef>
#include <cstdint>

namespace corehost::corepty
{

[[nodiscard]] inline std::size_t trim_row_for_scroll(const screen::screen_buffer &screen, std::size_t scroll_lines)
{
    if (scroll_lines == 0)
        return static_cast<std::size_t>(-1);
    const auto origin = screen.viewport_origin();
    const auto size = screen.viewport_size();
    return static_cast<std::size_t>(origin.y + size.y - static_cast<std::int32_t>(scroll_lines));
}

inline void note_terminal_cursor(screen::console_state &state, const screen::screen_buffer &screen,
                                 screen::coord cursor) noexcept
{
    const auto origin = screen.viewport_origin();
    state.note_terminal_cursor(cursor.x - origin.x, cursor.y - origin.y);
}

enum class render_frame_kind : std::uint8_t
{
    none,
    cursor_only,
    incremental,
    full,
};

// All render-relevant state is consumed exactly once into this snapshot. Fields
// are private so a frame can only be projected from take(), never assembled by
// an output path.
class render_snapshot final
{
  public:
    render_snapshot() = delete;
    render_snapshot(const render_snapshot &) = delete;
    render_snapshot &operator=(const render_snapshot &) = delete;
    render_snapshot(render_snapshot &&) noexcept = default;
    render_snapshot &operator=(render_snapshot &&) noexcept = default;

    [[nodiscard]] static render_snapshot take(screen::screen_buffer &screen, screen::console_state &console) noexcept;

    template <typename SessionState>
    [[nodiscard]] static render_snapshot take(screen::screen_buffer &screen, screen::console_state &console,
                                              SessionState &session) noexcept;

    [[nodiscard]] render_frame_kind kind() const noexcept
    {
        if (alternate_changed_ || viewport_changed_)
            return render_frame_kind::full;
        if (scroll_lines_ != 0 || damage_ || cursor_changed_ || cursor_visibility_changed_)
            return render_frame_kind::incremental;
        if (cursor_shape_changed_)
            return render_frame_kind::cursor_only;
        return render_frame_kind::none;
    }

    [[nodiscard]] std::size_t scroll_lines() const noexcept
    {
        return scroll_lines_;
    }

    [[nodiscard]] bool cursor_visibility_changed() const noexcept
    {
        return cursor_visibility_changed_;
    }

    [[nodiscard]] bool cursor_shape_changed() const noexcept
    {
        return cursor_shape_changed_;
    }

  private:
    render_snapshot(std::size_t scroll_lines, bool alternate_changed, bool viewport_changed, bool damage,
                    bool cursor_changed, bool cursor_visibility_changed, bool cursor_shape_changed) noexcept
        : scroll_lines_{scroll_lines}, alternate_changed_{alternate_changed}, viewport_changed_{viewport_changed},
          damage_{damage}, cursor_changed_{cursor_changed}, cursor_visibility_changed_{cursor_visibility_changed},
          cursor_shape_changed_{cursor_shape_changed}
    {
    }

    std::size_t scroll_lines_{};
    bool alternate_changed_{};
    bool viewport_changed_{};
    bool damage_{};
    bool cursor_changed_{};
    bool cursor_visibility_changed_{};
    bool cursor_shape_changed_{};
};

inline render_snapshot render_snapshot::take(screen::screen_buffer &screen, screen::console_state &console) noexcept
{
    return render_snapshot{screen.take_scroll_up(),
                           false,
                           false,
                           screen.has_damage(),
                           console.take_cursor_changed(),
                           console.take_cursor_visibility_changed(),
                           console.take_cursor_shape_changed()};
}

template <typename SessionState>
render_snapshot render_snapshot::take(screen::screen_buffer &screen, screen::console_state &console,
                                      SessionState &session) noexcept
{
    auto snapshot = take(screen, console);
    snapshot.alternate_changed_ = session.take_alternate_changed();
    snapshot.viewport_changed_ = session.take_viewport_changed();
    return snapshot;
}

inline void render_snapshot_frame(const render_snapshot &snapshot, screen::screen_buffer &screen,
                                  screen::console_state &console, vtserializer::byte_buffer &output)
{
    const auto cursor =
        screen::coord{static_cast<std::int32_t>(console.cursor().x), static_cast<std::int32_t>(console.cursor().y)};
    switch (snapshot.kind())
    {
    case render_frame_kind::full:
        vtserializer::render_viewport(output, screen, cursor, &console, false);
        screen.clear_damage();
        note_terminal_cursor(console, screen, cursor);
        break;
    case render_frame_kind::incremental:
        if (snapshot.scroll_lines() != 0)
            vtserializer::append_scroll_up(output, static_cast<unsigned>(snapshot.scroll_lines()));
        vtserializer::render_damage(output, screen, cursor, &console,
                                    trim_row_for_scroll(screen, snapshot.scroll_lines()), false);
        screen.clear_damage();
        note_terminal_cursor(console, screen, cursor);
        break;
    case render_frame_kind::cursor_only:
    case render_frame_kind::none:
        break;
    }
    if (snapshot.cursor_visibility_changed())
        vtserializer::append_cursor_visibility(output, console.cursor().visible);
    if (snapshot.cursor_shape_changed())
        vtserializer::append_cursor_shape(output, static_cast<unsigned>(console.cursor().style),
                                          console.cursor().blinking);
}

inline void render_full_viewport(screen::screen_buffer &screen, screen::console_state &console,
                                 vtserializer::byte_buffer &output)
{
    const auto snapshot = render_snapshot::take(screen, console);
    const auto cursor =
        screen::coord{static_cast<std::int32_t>(console.cursor().x), static_cast<std::int32_t>(console.cursor().y)};
    vtserializer::render_viewport(output, screen, cursor, &console, false);
    screen.clear_damage();
    note_terminal_cursor(console, screen, cursor);
    if (snapshot.cursor_visibility_changed())
        vtserializer::append_cursor_visibility(output, console.cursor().visible);
    if (snapshot.cursor_shape_changed())
        vtserializer::append_cursor_shape(output, static_cast<unsigned>(console.cursor().style),
                                          console.cursor().blinking);
}

inline void commit_direct_output(screen::screen_buffer &screen, screen::console_state &console) noexcept
{
    (void)screen.take_scroll_up();
    screen.clear_damage();
    (void)console.take_cursor_changed();
    const auto cursor =
        screen::coord{static_cast<std::int32_t>(console.cursor().x), static_cast<std::int32_t>(console.cursor().y)};
    note_terminal_cursor(console, screen, cursor);
}

template <typename SessionState>
inline void consume_render_state_after_raw(screen::screen_buffer &screen, screen::console_state &console,
                                           SessionState &session, vtserializer::byte_buffer &output)
{
    const auto snapshot = render_snapshot::take(screen, console, session);
    screen.clear_damage();
    console.invalidate_terminal_cursor();
    if (snapshot.cursor_visibility_changed())
        vtserializer::append_cursor_visibility(output, console.cursor().visible);
    if (snapshot.cursor_shape_changed())
        vtserializer::append_cursor_shape(output, static_cast<unsigned>(console.cursor().style),
                                          console.cursor().blinking);
}

inline void render_pending_screen(screen::screen_buffer &screen, screen::console_state &console,
                                  vtserializer::byte_buffer &output)
{
    render_snapshot_frame(render_snapshot::take(screen, console), screen, console, output);
}

template <typename SessionState>
inline void render_pending(screen::screen_buffer &screen, screen::console_state &console, SessionState &session,
                           vtserializer::byte_buffer &output)
{
    render_snapshot_frame(render_snapshot::take(screen, console, session), screen, console, output);
}

} // namespace corehost::corepty
