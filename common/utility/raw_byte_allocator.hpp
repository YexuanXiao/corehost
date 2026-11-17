#pragma once

#include <cstddef>
#include <limits>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

namespace corehost::utility
{

// Raw allocation skips construction/destruction for trivial values, which is
// what byte and character buffers need. Debug standard-library containers
// rebind allocators to internal non-trivial types such as _Container_proxy;
// that rebind is the only case allowed to fall back to normal construction.
template <typename T, bool EnforceTrivial = std::is_trivial_v<T>>
struct raw_byte_allocator_impl
{
    static_assert(!EnforceTrivial || std::is_trivial_v<T>, "raw_byte_allocator requires a trivial value type");

    using value_type = T;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using propagate_on_container_move_assignment = std::true_type;
    using is_always_equal = std::true_type;

    raw_byte_allocator_impl() noexcept = default;

    template <typename U, bool OtherEnforce>
    raw_byte_allocator_impl(const raw_byte_allocator_impl<U, OtherEnforce> &) noexcept
    {
    }

    [[nodiscard]] T *allocate(std::size_t count) noexcept
    {
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
    void construct(U *ptr) noexcept(std::is_nothrow_default_constructible_v<U>)
    {
        if constexpr (!std::is_trivial_v<U>)
            std::construct_at(ptr);
    }

    template <typename U, typename... Args>
    void construct(U *ptr, Args &&...args) noexcept(noexcept(std::construct_at(ptr, std::forward<Args>(args)...)))
    {
        std::construct_at(ptr, std::forward<Args>(args)...);
    }

    template <typename U>
    void destroy(U *ptr) noexcept
    {
        if constexpr (!std::is_trivial_v<U>)
            std::destroy_at(ptr);
    }

    template <typename U>
    struct rebind
    {
        using other = raw_byte_allocator_impl<U, std::is_trivial_v<U>>;
    };
};

template <typename T>
using raw_byte_allocator = raw_byte_allocator_impl<T>;

template <typename T>
struct raw_vector_allocator : raw_byte_allocator<T>
{
    static_assert(std::is_trivial_v<T>, "raw_byte_vector requires a trivial value type");
    using raw_byte_allocator<T>::raw_byte_allocator;
};

template <typename T>
struct raw_string_allocator : raw_byte_allocator<T>
{
    static_assert(std::is_trivial_v<T>, "raw_byte_string requires a trivial value type");
    using raw_byte_allocator<T>::raw_byte_allocator;
};

template <typename T>
using raw_byte_vector = std::vector<T, raw_vector_allocator<T>>;

} // namespace corehost::utility
