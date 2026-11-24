#include "cell_atlas.hpp"
#include "text_engine.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <vector>

int main()
{
    try
    {
        constexpr DWRITE_UNICODE_RANGE latin{0x20, 0x7E};
        constexpr DWRITE_UNICODE_RANGE emoji{0x1F300, 0x1FAFF};
        const std::array<corehost::uitext::font_fallback_entry, 4> families{
            corehost::uitext::font_fallback_entry{L"Cascadia Code"},
            corehost::uitext::font_fallback_entry{L"Consolas", latin},
            corehost::uitext::font_fallback_entry{L"Segoe UI Emoji", emoji},
            corehost::uitext::font_fallback_entry{L"Segoe UI Symbol"},
        };
        corehost::uitext::text_engine engine{families, 16.0f};
        const auto metrics = engine.metrics();
        if (metrics.advance <= 0.0f || metrics.ascent <= 0.0f || metrics.descent < 0.0f)
        {
            std::fprintf(stderr, "invalid text metrics\n");
            return EXIT_FAILURE;
        }

        corehost::uitext::cell_atlas atlas{engine};
        const auto *first = atlas.find_or_add(u'M');
        const auto *second = atlas.find_or_add(u'M');
        if (first == nullptr || first != second || first->width == 0 || first->height == 0 || !atlas.dirty())
        {
            std::fprintf(stderr, "cell atlas cache failed\n");
            return EXIT_FAILURE;
        }
        atlas.clear_dirty();

        std::vector<corehost::uitext::glyph_bitmap> glyphs;
        const auto *bold = atlas.find_or_add_transient(u"M", true);
        if (bold == nullptr || bold->width == 0 || bold->height == 0)
        {
            std::fprintf(stderr, "bold cell atlas failed\n");
            return EXIT_FAILURE;
        }

        const auto *non_bmp = atlas.find_or_add_transient(u"\U0001F600");
        if (non_bmp == nullptr || non_bmp->width == 0)
        {
            std::fprintf(stderr, "non-BMP transient atlas failed\n");
            return EXIT_FAILURE;
        }

        atlas.begin_frame();
        const auto *cluster = atlas.find_or_add_transient(u"e\u0301");
        if (cluster == nullptr || cluster->width == 0)
        {
            std::fprintf(stderr, "grapheme cluster transient atlas failed\n");
            return EXIT_FAILURE;
        }

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

        engine.rasterize(u"e\u0301", glyphs);
        if (glyphs.empty() || glyphs.front().alpha.empty())
        {
            std::fprintf(stderr, "combining sequence rasterization failed\n");
            return EXIT_FAILURE;
        }

        engine.rasterize(u"\U0001F600", glyphs);
        if (glyphs.empty() || glyphs.front().alpha.empty())
        {
            std::fprintf(stderr, "fallback font list failed for emoji\n");
            return EXIT_FAILURE;
        }

        engine.rasterize(u"M", glyphs, true);
        if (glyphs.empty() || glyphs.front().alpha.empty())
        {
            std::fprintf(stderr, "bold rasterization failed\n");
            return EXIT_FAILURE;
        }

        engine.rasterize(u"ab", glyphs);
        if (glyphs.empty())
        {
            std::fprintf(stderr, "multi-character layout rasterization failed\n");
            return EXIT_FAILURE;
        }

        const auto base_advance = engine.metrics().advance;
        engine.set_size(32.0f);
        if (engine.metrics().advance <= base_advance)
        {
            std::fprintf(stderr, "DPI size scaling failed\n");
            return EXIT_FAILURE;
        }
        engine.set_size(16.0f);
    }
    catch (...)
    {
        std::fprintf(stderr, "text engine initialization failed\n");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
