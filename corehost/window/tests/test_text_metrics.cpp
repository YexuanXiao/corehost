#include "text_engine.hpp"

#include <cstdio>
#include <cstdlib>

int main()
{
    try
    {
        const corehost::uitext::text_engine engine{L"Consolas", 16.0f};
        const auto metrics = engine.metrics();
        const auto glyph = engine.rasterize(u'M');
        if (glyph.width <= 0 || glyph.height <= 0 || glyph.alpha.empty())
        {
            std::fprintf(stderr, "glyph rasterization failed: %d x %d\n", glyph.width, glyph.height);
            return EXIT_FAILURE;
        }
        if (metrics.advance <= 0.0f || metrics.ascent <= 0.0f || metrics.descent < 0.0f)
        {
            std::fprintf(stderr, "invalid text metrics\n");
            return EXIT_FAILURE;
        }
    }
    catch (...)
    {
        std::fprintf(stderr, "text engine initialization failed\n");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
