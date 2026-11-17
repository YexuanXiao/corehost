#pragma once

#include "console_state.hpp"
#include "control.hpp"
#include "screen_buffer.hpp"
#include "viewport.hpp"

#include <cstddef>
#include <cstdint>

namespace corehost::corehost2
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
    state.terminal_cursor_x = cursor.x - origin.x;
    state.terminal_cursor_y = cursor.y - origin.y;
    state.terminal_cursor_valid = true;
}

enum class render_frame_kind : std::uint8_t
{
    none,
    cursor_only,
    incremental,
    full,
};

// All render-relevant state is consumed exactly once into this snapshot. A
// render frame is then either a complete viewport, an incremental damage frame,
// or a cursor-only update; no caller has to pair individual take_* calls.
struct render_snapshot final
{
    std::size_t scroll_lines{};
    bool alternate_changed{};
    bool viewport_changed{};
    bool damage{};
    bool cursor_changed{};
    bool cursor_visibility_changed{};
    bool cursor_shape_changed{};

    [[nodiscard]] render_frame_kind kind() const noexcept
    {
        if (alternate_changed || viewport_changed)
            return render_frame_kind::full;
        if (scroll_lines != 0 || damage || cursor_changed || cursor_visibility_changed)
            return render_frame_kind::incremental;
        if (cursor_shape_changed)
            return render_frame_kind::cursor_only;
        return render_frame_kind::none;
    }
};

[[nodiscard]] inline render_snapshot take_render_snapshot(screen::screen_buffer &screen,
                                                          screen::console_state &console) noexcept
{
    render_snapshot snapshot;
    snapshot.scroll_lines = screen.take_scroll_up();
    snapshot.damage = screen.has_damage();
    snapshot.cursor_changed = console.take_cursor_changed();
    snapshot.cursor_visibility_changed = console.take_cursor_visibility_changed();
    snapshot.cursor_shape_changed = console.take_cursor_shape_changed();
    return snapshot;
}

template <typename SessionState>
[[nodiscard]] inline render_snapshot take_render_snapshot(screen::screen_buffer &screen,
                                                          screen::console_state &console,
                                                          SessionState &session) noexcept
{
    auto snapshot = take_render_snapshot(screen, console);
    snapshot.alternate_changed = session.take_alternate_changed();
    snapshot.viewport_changed = session.take_viewport_changed();
    return snapshot;
}

inline void render_snapshot_frame(const render_snapshot &snapshot, screen::screen_buffer &screen,
                                  screen::console_state &console, vtserializer::byte_buffer &output)
{
    const auto cursor = screen::coord{static_cast<std::int32_t>(console.cursor().x),
                                      static_cast<std::int32_t>(console.cursor().y)};
    switch (snapshot.kind())
    {
    case render_frame_kind::full:
        vtserializer::render_viewport(output, screen, cursor, &console, false);
        screen.clear_damage();
        note_terminal_cursor(console, screen, cursor);
        break;
    case render_frame_kind::incremental:
        if (snapshot.scroll_lines != 0)
            vtserializer::append_scroll_up(output, static_cast<unsigned>(snapshot.scroll_lines));
        vtserializer::render_damage(output, screen, cursor, &console,
                                    trim_row_for_scroll(screen, snapshot.scroll_lines), false);
        screen.clear_damage();
        note_terminal_cursor(console, screen, cursor);
        break;
    case render_frame_kind::cursor_only:
    case render_frame_kind::none:
        break;
    }
    if (snapshot.cursor_visibility_changed)
        vtserializer::append_cursor_visibility(output, console.cursor().visible);
    if (snapshot.cursor_shape_changed)
        vtserializer::append_cursor_shape(output, static_cast<unsigned>(console.cursor().style),
                                          console.cursor().blinking);
}

inline void render_full_viewport(screen::screen_buffer &screen, screen::console_state &console,
                                 vtserializer::byte_buffer &output)
{
    const auto snapshot = take_render_snapshot(screen, console);
    const auto cursor = screen::coord{static_cast<std::int32_t>(console.cursor().x),
                                      static_cast<std::int32_t>(console.cursor().y)};
    vtserializer::render_viewport(output, screen, cursor, &console, false);
    screen.clear_damage();
    note_terminal_cursor(console, screen, cursor);
    if (snapshot.cursor_visibility_changed)
        vtserializer::append_cursor_visibility(output, console.cursor().visible);
    if (snapshot.cursor_shape_changed)
        vtserializer::append_cursor_shape(output, static_cast<unsigned>(console.cursor().style),
                                          console.cursor().blinking);
}

inline void commit_direct_output(screen::screen_buffer &screen, screen::console_state &console) noexcept
{
    (void)screen.take_scroll_up();
    screen.clear_damage();
    (void)console.take_cursor_changed();
    const auto cursor = screen::coord{static_cast<std::int32_t>(console.cursor().x),
                                      static_cast<std::int32_t>(console.cursor().y)};
    note_terminal_cursor(console, screen, cursor);
}

template <typename SessionState>
inline void consume_render_state_after_raw(screen::screen_buffer &screen, screen::console_state &console,
                                           SessionState &session, vtserializer::byte_buffer &output)
{
    const auto snapshot = take_render_snapshot(screen, console, session);
    screen.clear_damage();
    console.terminal_cursor_valid = false;
    if (snapshot.cursor_shape_changed)
        vtserializer::append_cursor_shape(output, static_cast<unsigned>(console.cursor().style),
                                          console.cursor().blinking);
}

inline void render_pending_screen(screen::screen_buffer &screen, screen::console_state &console,
                                  vtserializer::byte_buffer &output)
{
    const auto snapshot = take_render_snapshot(screen, console);
    render_snapshot_frame(snapshot, screen, console, output);
}

template <typename SessionState>
inline void render_pending(screen::screen_buffer &screen, screen::console_state &console, SessionState &session,
                           vtserializer::byte_buffer &output)
{
    const auto snapshot = take_render_snapshot(screen, console, session);
    render_snapshot_frame(snapshot, screen, console, output);
}

} // namespace corehost::corehost2
