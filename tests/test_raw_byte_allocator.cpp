#include "raw_byte_allocator.hpp"

#include <vector>

#define ASSERT_TRUE_BOOL(expr)                                                                                         \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(expr))                                                                                                   \
            return false;                                                                                              \
    } while (false)

#define ASSERT_TRUE_MAIN(expr)                                                                                         \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(expr))                                                                                                   \
            return 1;                                                                                                  \
    } while (false)

template <typename T>
bool preserves_existing_values_after_resize()
{
    std::vector<T, corehost::conpty::raw_byte_allocator<T>> values;
    values.reserve(2);
    values.push_back(static_cast<T>(1));
    values.push_back(static_cast<T>(2));

    const auto old_capacity = values.capacity();
    values.resize(old_capacity + 8);

    ASSERT_TRUE_BOOL(values[0] == static_cast<T>(1));
    ASSERT_TRUE_BOOL(values[1] == static_cast<T>(2));

    for (size_t i = 2; i < values.size(); ++i)
        values[i] = static_cast<T>(i + 1);
    for (size_t i = 2; i < values.size(); ++i)
        ASSERT_TRUE_BOOL(values[i] == static_cast<T>(i + 1));

    return true;
}

int main()
{
    ASSERT_TRUE_MAIN(preserves_existing_values_after_resize<char8_t>());
    ASSERT_TRUE_MAIN(preserves_existing_values_after_resize<char16_t>());
    ASSERT_TRUE_MAIN(preserves_existing_values_after_resize<wchar_t>());
    ASSERT_TRUE_MAIN(preserves_existing_values_after_resize<char32_t>());
    return 0;
}
