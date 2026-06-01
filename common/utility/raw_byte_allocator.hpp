#pragma once
#include <cstddef>
#include <limits>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

namespace conpty
{

template <typename T>
inline constexpr bool raw_byte_allocator_value =
    std::is_same_v<T, char8_t> || std::is_same_v<T, char16_t> || std::is_same_v<T, wchar_t> ||
    std::is_same_v<T, char32_t>;

template <typename T>
struct raw_byte_allocator
{
    static_assert(raw_byte_allocator_value<T>, "raw_byte_allocator is only for raw character buffers");

    using value_type = T;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using propagate_on_container_move_assignment = std::true_type;
    using is_always_equal = std::true_type;

    raw_byte_allocator() noexcept = default;

    template <typename U>
    raw_byte_allocator(const raw_byte_allocator<U> &) noexcept
    {
        static_assert(raw_byte_allocator_value<U>, "raw_byte_allocator is only for raw character buffers");
    }

    [[nodiscard]] T *allocate(std::size_t count)
    {
        if (count > max_size())
            throw std::bad_array_new_length{};
        return static_cast<T *>(::operator new(count * sizeof(T)));
    }

    void deallocate(T *ptr, std::size_t) noexcept
    {
        ::operator delete(ptr);
    }

    [[nodiscard]] constexpr std::size_t max_size() const noexcept
    {
        return std::numeric_limits<std::size_t>::max() / sizeof(T);
    }

    template <typename U>
    void construct(U *) noexcept
    {
        static_assert(raw_byte_allocator_value<U>, "raw_byte_allocator is only for raw character buffers");
    }

    template <typename U, typename... Args>
    void construct(U *ptr, Args &&...args) noexcept(noexcept(std::construct_at(ptr, std::forward<Args>(args)...)))
    {
        static_assert(raw_byte_allocator_value<U>, "raw_byte_allocator is only for raw character buffers");
        std::construct_at(ptr, std::forward<Args>(args)...);
    }

    template <typename U>
    void destroy(U *) noexcept
    {
        static_assert(raw_byte_allocator_value<U>, "raw_byte_allocator is only for raw character buffers");
    }

    template <typename U>
    struct rebind
    {
        using other = raw_byte_allocator<U>;
    };
};

template <typename T>
using raw_byte_vector = std::vector<T, raw_byte_allocator<T>>;

using raw_u8_buffer = raw_byte_vector<char8_t>;
using raw_u16_buffer = raw_byte_vector<char16_t>;
using raw_wide_buffer = raw_byte_vector<wchar_t>;
using raw_u32_buffer = raw_byte_vector<char32_t>;

template <typename T, typename U>
constexpr bool operator==(const raw_byte_allocator<T> &, const raw_byte_allocator<U> &) noexcept
{
    return true;
}

template <typename T, typename U>
constexpr bool operator!=(const raw_byte_allocator<T> &, const raw_byte_allocator<U> &) noexcept
{
    return false;
}

} // namespace conpty
