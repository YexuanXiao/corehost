#include "text_engine.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <vector>

int main()
{
    try
    {
        const std::array<win32::wcstring_view, 3> families{L"Consolas", L"Segoe UI Symbol", L"Segoe UI Emoji"};
        corehost::uitext::text_engine engine{families, 16.0f};
        const auto metrics = engine.metrics();
        if (metrics.advance <= 0.0f || metrics.ascent <= 0.0f || metrics.descent < 0.0f)
        {
            std::fprintf(stderr, "invalid text metrics\n");
            return EXIT_FAILURE;
        }

        std::vector<corehost::uitext::glyph_bitmap> glyphs;
        engine.rasterize(u"M", glyphs);
        if (glyphs.size() != 1)
        {
            std::fprintf(stderr, "expected one glyph cluster\n");
            return EXIT_FAILURE;
        }

        const auto &glyph = glyphs.front();
        if (glyph.width <= 0 || glyph.height <= 0 || glyph.alpha.empty())
        {
            std::fprintf(stderr, "glyph rasterization failed: %d x %d\n", glyph.width, glyph.height);
            return EXIT_FAILURE;
        }

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

        std::vector<corehost::uitext::glyph_bitmap> clusters;
        engine.rasterize(u"e\u0301", clusters);
        if (clusters.size() != 1)
        {
            std::fprintf(stderr, "combining sequence was not one grapheme cluster\n");
            return EXIT_FAILURE;
        }

        engine.rasterize(u"\U0001F600", clusters);
        if (clusters.empty() || clusters.front().alpha.empty())
        {
            std::fprintf(stderr, "fallback font list failed for emoji\n");
            return EXIT_FAILURE;
        }
        engine.rasterize(u"ab", clusters);
        if (clusters.size() != 2)
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
