#include "text_engine.hpp"

#include <cstdio>
#include <cstdlib>

int main()
{
    try
    {
        const corehost::uitext::text_engine engine{L"Consolas", 16.0f};
        const auto metrics = engine.metrics();
        const auto glyphs = engine.rasterize(L"M");
        if (glyphs.size() != 1)
        {
            std::fprintf(stderr, "expected one glyph cluster\n");
            return EXIT_FAILURE;
        }
        const auto &glyph = glyphs.front();
        bool has_grayscale = false;
        for (const auto value : glyph.alpha)
        {
            has_grayscale |= value != 0 && value != 255;
        }
        if (!has_grayscale)
        {
            std::fprintf(stderr, "glyph is not grayscale antialiased\n");
            return EXIT_FAILURE;
        }
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

        const auto clusters = engine.rasterize(L"e\u0301");
        if (clusters.size() != 1)
        {
            std::fprintf(stderr, "combining sequence was not one grapheme cluster\n");
            return EXIT_FAILURE;
        }

        const auto letters = engine.rasterize(L"ab");
        if (letters.size() != 2)
        {
            std::fprintf(stderr, "expected two grapheme clusters\n");
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
