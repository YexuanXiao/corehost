#pragma once
#include <type_traits>

namespace win32
{

enum class invalid_state
{
};

enum class invalid_argument
{
};

template <typename T>
    requires std::is_scalar_v<T>
constexpr void check_null(T value)
{
    if (value == T{})
    {
        throw detail::invalid_state{};
    }
}
template <typename T>
    requires std::is_scalar_v<T>
constexpr void check_not_null(T value)
{
    if (value == T{})
    {
        throw detail::invalid_state{};
    }
}

template <typename... Args>
void check_one(Args... args)
{
    if (!(... || args))
    {
        throw detail::invalid_argument{};
    }
}

template <typename... Args>
void check_all(Args... args)
{
    if (!(... && args))
    {
        throw detail::invalid_argument{};
    }
}

#ifdef __cpp_lib_unreachable
using std::unreachable;
#else
void unreachable [[noreturn]] () noexcept
{
    std::terminate();
}
#endif

} // namespace win32
