#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

// ABI-compatible, header-only Win32 console types. This file deliberately does
// not include <windows.h>; libinterop is the only place that owns these layouts.
namespace corehost::interop::win32
{

using byte = std::uint8_t;
using word = std::uint16_t;
using dword = std::uint32_t;
using boolean = std::int32_t;

struct alignas(2) coord final
{
    std::int16_t x{};
    std::int16_t y{};

    friend constexpr bool operator==(const coord &, const coord &) noexcept = default;
};

struct alignas(2) small_rect final
{
    std::int16_t left{};
    std::int16_t top{};
    std::int16_t right{};
    std::int16_t bottom{};

    friend constexpr bool operator==(const small_rect &, const small_rect &) noexcept = default;
};

union char_union final
{
    char16_t unicode_char;
    char ascii_char;
};

struct alignas(2) char_info final
{
    char_union char_data;
    word attributes{};
};

struct alignas(2) console_screen_buffer_info final
{
    coord size{};
    coord cursor_position{};
    word attributes{};
    small_rect window{};
    coord maximum_window_size{};
};

struct alignas(4) console_cursor_info final
{
    dword size{};
    boolean visible{};
};

struct alignas(4) console_selection_info final
{
    dword flags{};
    coord selection_anchor{};
    coord selection{};
};

struct alignas(4) console_font_info final
{
    dword font_index{};
    coord font_size{};
};

struct alignas(4) console_font_infoex final
{
    dword cb_size{sizeof(console_font_infoex)};
    dword font_index{};
    std::int16_t font_width{};
    std::int16_t font_height{};
    dword font_family{};
    dword font_weight{400};
    char16_t face_name[32]{u"C"};
};

inline constexpr word default_attributes = 0x0007;
inline constexpr word lvb_leading_byte = 0x0100;
inline constexpr word lvb_trailing_byte = 0x0200;
inline constexpr word lvb_grid_horizontal = 0x0400;
inline constexpr word lvb_grid_lvertical = 0x0800;
inline constexpr word lvb_grid_rvertical = 0x1000;
inline constexpr word lvb_reverse_video = 0x4000;
inline constexpr word lvb_underscore = 0x8000;

inline constexpr dword console_selection_active = 0x0001;
inline constexpr dword console_no_selection = 0x0000;

inline constexpr dword enable_processed_output = 0x0001;
inline constexpr dword enable_wrap_at_eol_output = 0x0002;
inline constexpr dword enable_virtual_terminal_processing = 0x0004;

static_assert(sizeof(coord) == 4 && alignof(coord) == 2);
static_assert(sizeof(small_rect) == 8 && alignof(small_rect) == 2);
static_assert(sizeof(char_info) == 4 && alignof(char_info) == 2);
static_assert(sizeof(console_screen_buffer_info) == 22 && alignof(console_screen_buffer_info) == 2);
static_assert(sizeof(console_cursor_info) == 8 && alignof(console_cursor_info) == 4);
static_assert(sizeof(console_selection_info) == 12 && alignof(console_selection_info) == 4);
static_assert(sizeof(console_font_info) == 8 && alignof(console_font_info) == 4);
static_assert(sizeof(console_font_infoex) == 84 && alignof(console_font_infoex) == 4);
static_assert(offsetof(console_screen_buffer_info, cursor_position) == 4);
static_assert(offsetof(console_screen_buffer_info, attributes) == 8);
static_assert(offsetof(console_screen_buffer_info, window) == 10);
static_assert(offsetof(console_screen_buffer_info, maximum_window_size) == 18);
static_assert(offsetof(char_info, attributes) == 2);

} // namespace corehost::interop::win32
