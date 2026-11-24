#include "scrollbar.hpp"

#include <cstdio>
#include <cstdlib>

namespace
{

int failures = 0;

void check(bool value, const char *message)
{
    if (!value)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

} // namespace

int main()
{
    using namespace corehost::scrollbar;

    const state bottom{100, 20, 0};
    const auto full = compute(0.0f, 0.0f, default_width, 200.0f, bottom);
    check(full.visible, "visible when scrollback exists");
    check(full.thumb_height > minimum_thumb_height, "thumb has proportional height");
    check(full.thumb_y >= full.track_y, "thumb starts inside arrow area");

    const auto top = compute(0.0f, 0.0f, default_width, 200.0f, state{100, 20, static_cast<double>(max_top(bottom))});
    check(top.thumb_y + top.thumb_height <= 200.0f - 26.0f, "thumb stays inside arrow area");

    check(hit_test(full, 4.0f, 2.0f) == hit_target::arrow_up, "hit up arrow");
    check(hit_test(full, 4.0f, 197.0f) == hit_target::arrow_down, "hit down arrow");
    check(hit_test(full, 4.0f, 30.0f) == hit_target::thumb, "hit thumb");
    check(hit_test(full, 4.0f, 160.0f) == hit_target::below_thumb, "hit below thumb");
    check(hit_test(full, 17.0f, 2.0f) == hit_target::none, "outside scrollbar");

    check(top_from_pointer(top, 199.0f, top.thumb_height, bottom) == max_top(bottom),
          "pointer at bottom maps to max top");
    check(top_from_pointer(full, 0.0f, 0.0f, bottom) == 0, "pointer at top maps to zero");

    const auto hidden = compute(0.0f, 0.0f, default_width, 200.0f, state{20, 20, 0});
    check(!hidden.visible, "hidden without scrollback");
    check(hit_test(hidden, 4.0f, 50.0f) == hit_target::none, "hidden scrollbar ignores hit");

    if (failures != 0)
    {
        std::fprintf(stderr, "%d failures\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("scrollbar geometry tests passed\n");
    return EXIT_SUCCESS;
}
