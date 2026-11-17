#include "console_state.hpp"
#include "screen_buffer.hpp"

#include <chrono>
#include <cstdio>
#include <vector>

using clock_type = std::chrono::steady_clock;

double ns_per_op(clock_type::duration elapsed, std::size_t iterations)
{
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();
    return static_cast<double>(ns) / static_cast<double>(iterations);
}

void bench_scalar()
{
    corehost::screen::screen_buffer buffer(120, 30, 1000);
    constexpr std::size_t iterations = 2'000'000;
    const auto begin = clock_type::now();
    for (std::size_t i = 0; i < iterations; ++i)
    {
        (void)buffer.write_scalar(i % 120, (i / 120) % 30, u'A');
    }
    const auto end = clock_type::now();
    std::printf("scalar write: %.2f ns/op\n", ns_per_op(end - begin, iterations));
}

void bench_row_text()
{
    corehost::screen::screen_buffer buffer(120, 30, 1000);
    constexpr std::size_t iterations = 200'000;
    const auto begin = clock_type::now();
    for (std::size_t i = 0; i < iterations; ++i)
    {
        corehost::screen::coord cursor{0, 0};
        (void)buffer.write_text_row(cursor, u"hello world", {}, corehost::screen::cell_style{0});
    }
    const auto end = clock_type::now();
    std::printf("text row: %.2f ns/op\n", ns_per_op(end - begin, iterations));
}

void bench_scroll()
{
    corehost::screen::screen_buffer buffer(120, 30, 1000);
    constexpr std::size_t iterations = 100'000;
    const auto begin = clock_type::now();
    for (std::size_t i = 0; i < iterations; ++i)
    {
        buffer.scroll_up(1);
    }
    const auto end = clock_type::now();
    std::printf("scroll_up: %.2f ns/op\n", ns_per_op(end - begin, iterations));
}

void bench_char_info()
{
    corehost::screen::screen_buffer buffer(120, 30, 1000);
    std::vector<corehost::screen::char_info> cells(120 * 30);
    constexpr std::size_t iterations = 100'000;
    const auto begin = clock_type::now();
    for (std::size_t i = 0; i < iterations; ++i)
    {
        (void)buffer.read_char_info(corehost::screen::rect{0, 0, 119, 29}, cells);
    }
    const auto end = clock_type::now();
    std::printf("read_char_info 120x30: %.2f ns/op\n", ns_per_op(end - begin, iterations));
}

int main()
{
    bench_scalar();
    bench_row_text();
    bench_scroll();
    bench_char_info();
    return 0;
}