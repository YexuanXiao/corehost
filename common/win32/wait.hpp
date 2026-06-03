#pragma once

#include "win32/error.hpp"

#include <Windows.h>
#include <array>
#include <concepts>
#include <cstddef>
#include <type_traits>
#include <tuple>
#include <utility>

namespace win32
{

inline constexpr DWORD wait_signaled_base = 0x00000000;
inline constexpr DWORD wait_abandoned_base = 0x00000080;
inline constexpr DWORD wait_timeout_value = 0x00000102;
inline constexpr DWORD wait_failed_value = 0xffffffff;

enum class wait_status
{
    signaled,
    timeout,
    abandoned,
};

struct wait_result
{
    wait_status status{};
    size_t index{};

    [[nodiscard]] bool signaled() const noexcept
    {
        return status == wait_status::signaled;
    }

    [[nodiscard]] bool timeout() const noexcept
    {
        return status == wait_status::timeout;
    }

    [[nodiscard]] bool abandoned() const noexcept
    {
        return status == wait_status::abandoned;
    }
};

template <typename T>
concept waitable = requires(const T &object) {
    { object.get() } -> std::convertible_to<HANDLE>;
};

template <waitable T>
[[nodiscard]] wait_result wait_one(const T &object, DWORD timeout_ms)
{
    const DWORD result = ::WaitForSingleObject(object.get(), timeout_ms);
    if (result == wait_signaled_base)
        return {wait_status::signaled, 0};
    if (result == wait_timeout_value)
        return {wait_status::timeout, 0};
    if (result == wait_abandoned_base)
        return {wait_status::abandoned, 0};
    win32::throw_last_error(result == wait_failed_value);

    std::unreachable();
}

namespace details
{

template <typename Tuple, typename Indices>
struct wait_any_objects;

template <typename Tuple, size_t... Indices>
struct wait_any_objects<Tuple, std::index_sequence<Indices...>>
{
    static constexpr bool all_waitable =
        (waitable<std::remove_reference_t<std::tuple_element_t<Indices, Tuple>>> && ...);
};

template <typename Tuple, size_t... Indices>
[[nodiscard]] wait_result wait_any_impl(const Tuple &args, std::index_sequence<Indices...>)
{
    auto handles = std::array<HANDLE, sizeof...(Indices)>{std::get<Indices>(args).get()...};
    const auto timeout_ms = static_cast<DWORD>(std::get<sizeof...(Indices)>(args));
    const DWORD result =
        ::WaitForMultipleObjects(static_cast<DWORD>(handles.size()), handles.data(), FALSE, timeout_ms);
    if (result >= wait_signaled_base && result < wait_signaled_base + handles.size())
        return {wait_status::signaled, result - wait_signaled_base};
    if (result == wait_timeout_value)
        return {wait_status::timeout, 0};
    if (result >= wait_abandoned_base && result < wait_abandoned_base + handles.size())
        return {wait_status::abandoned, result - wait_abandoned_base};
    win32::throw_last_error(result == wait_failed_value);

    std::unreachable();
}

} // namespace details

template <typename... Args>
    requires(sizeof...(Args) >= 2)
[[nodiscard]] wait_result wait_any(const Args &...args)
{
    auto tuple = std::tuple<const Args &...>{args...};
    using object_indices = std::make_index_sequence<sizeof...(Args) - 1>;
    using last_arg = std::remove_cvref_t<std::tuple_element_t<sizeof...(Args) - 1, decltype(tuple)>>;
    static_assert(std::convertible_to<last_arg, DWORD>, "wait_any timeout must be the last argument");
    static_assert(details::wait_any_objects<decltype(tuple), object_indices>::all_waitable,
                  "wait_any objects must provide get() returning HANDLE");
    return details::wait_any_impl(tuple, object_indices{});
}

} // namespace win32
