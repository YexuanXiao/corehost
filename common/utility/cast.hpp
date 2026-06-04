#pragma once
#include <cstdlib>
#include <type_traits>
#include <concepts>
namespace utility
{
template <typename T, typename U>
    requires std::unsigned_integral<T> && std::unsigned_integral<U>
constexpr T narrow_cast(U u)
{
    static_assert(sizeof(U) >= sizeof(T));
    if (u > U(T(-1)))
    {
        std::abort();
    }
    return T(u);
}
template <typename T, typename U>
    requires std::signed_integral<T> && std::signed_integral<U>
constexpr T narrow_cast(U u)
{
    static_assert(sizeof(U) >= sizeof(T));
    if (u < 0 || u > (std::make_unsigned_t<T>(-1) >> 1))
    {
        std::abort();
    }
    return T(u);
}
template <typename T, typename U>
T narrow_cast(U)
{
    static_assert(false);
}
} // namespace utility
