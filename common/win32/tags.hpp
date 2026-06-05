#pragma once

// ── Tag types for constructor selection ─────────────────────────
//
// These tag types are used to disambiguate constructors that have
// different semantics (e.g. open an existing resource vs create a new one).
//
// Usage:
//   auto key = win32::registry_key{win32::open_tag, ...};
//   auto ev  = win32::event{win32::create_tag, ...};
//   auto ev  = win32::event{win32::open_tag, ...};

namespace win32
{

struct open_tag_t
{
};
inline constexpr open_tag_t open_tag{};

struct create_tag_t
{
};
inline constexpr create_tag_t create_tag{};

} // namespace win32
