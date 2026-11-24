#pragma once

#include "text_writer.hpp"
#include "win32/string.hpp"

#include <dwrite.h>
#include <dwrite_2.h>
#include <winrt/base.h>

#include <cassert>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace corehost::uitext
{

struct cell_metrics final
{
    float ascent{};
    float descent{};
    float line_gap{};
    float advance{};
};

struct glyph_bitmap final
{
    int width{};
    int height{};
    int left{};
    int top{};
    std::span<const std::uint8_t> alpha;
};

// Header-only DirectWrite metrics and grapheme-cluster rasterization.
//
// The font family argument is an ordered fallback list. A family that is not
// installed is skipped; if no family resolves, construction fails. There is no
// hidden fallback family.
//
// Rasterization reuses the output vector and the engine's alpha/region
// scratch buffers. The spans in glyph_bitmap point into the engine and stay
// valid until the next rasterize() call.
class text_engine final
{
  public:
    explicit text_engine(std::span<const win32::wcstring_view> families, float size) : size_{size}
    {
        assert(!families.empty());
        assert(size_ > 0.0f);

        winrt::com_ptr<IUnknown> unknown;
        winrt::check_hresult(
            ::DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), unknown.put()));
        factory_ = unknown.as<IDWriteFactory>();

        winrt::com_ptr<IDWriteFontCollection> collection;
        winrt::check_hresult(factory_->GetSystemFontCollection(collection.put(), FALSE));

        for (const auto family : families)
        {
            assert(!family.empty());
            UINT32 family_index = 0;
            BOOL exists = FALSE;
            winrt::check_hresult(collection->FindFamilyName(family.c_str(), &family_index, &exists));
            if (exists == FALSE)
            {
                continue;
            }

            winrt::com_ptr<IDWriteFontFamily> font_family;
            winrt::check_hresult(collection->GetFontFamily(family_index, font_family.put()));

            winrt::com_ptr<IDWriteFont> font;
            winrt::check_hresult(font_family->GetFirstMatchingFont(
                DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, font.put()));

            winrt::com_ptr<IDWriteFontFace> face;
            winrt::check_hresult(font->CreateFontFace(face.put()));
            faces_.push_back(std::move(face));
        }

        assert(!faces_.empty());
        if (faces_.empty())
        {
            winrt::throw_hresult(DWRITE_E_NOFONT);
        }

        DWRITE_FONT_METRICS font_metrics{};
        faces_.front()->GetMetrics(&font_metrics);
        units_per_em_ = static_cast<float>(font_metrics.designUnitsPerEm);
        assert(units_per_em_ > 0.0f);
        metrics_.ascent = size_ * static_cast<float>(font_metrics.ascent) / units_per_em_;
        metrics_.descent = size_ * static_cast<float>(font_metrics.descent) / units_per_em_;
        metrics_.line_gap = size_ * static_cast<float>(font_metrics.lineGap) / units_per_em_;

        const UINT32 sample = u'M';
        UINT16 glyph = 0;
        winrt::check_hresult(faces_.front()->GetGlyphIndices(&sample, 1, &glyph));
        DWRITE_GLYPH_METRICS glyph_metrics{};
        winrt::check_hresult(faces_.front()->GetDesignGlyphMetrics(&glyph, 1, &glyph_metrics, FALSE));
        metrics_.advance = size_ * static_cast<float>(glyph_metrics.advanceWidth) / units_per_em_;
    }

    text_engine(const text_engine &) = delete;
    text_engine &operator=(const text_engine &) = delete;

    [[nodiscard]] const cell_metrics &metrics() const noexcept
    {
        return metrics_;
    }

    void rasterize(std::u16string_view text, std::vector<glyph_bitmap> &out)
    {
        out.clear();
        regions_.clear();
        alpha_storage_.clear();

        for (const auto cluster : screen::grapheme_range<screen::text_measurement::graphemes>{text})
        {
            rasterize_cluster(cluster.text);
        }

        out.reserve(regions_.size());
        for (const auto &region : regions_)
        {
            out.push_back(glyph_bitmap{
                .width = region.width,
                .height = region.height,
                .left = region.left,
                .top = region.top,
                .alpha = std::span<const std::uint8_t>{alpha_storage_.data() + region.offset, region.size},
            });
        }
    }

  private:
    struct glyph_region final
    {
        int width{};
        int height{};
        int left{};
        int top{};
        std::size_t offset{};
        std::size_t size{};
    };

    bool collect_glyphs(IDWriteFontFace *face, std::span<const char16_t> cluster)
    {
        glyph_indices_.clear();
        advances_.clear();

        const std::u16string_view view{cluster.data(), cluster.size()};
        std::size_t offset = 0;
        while (offset < view.size())
        {
            const auto decoded = screen::next_codepoint(view, offset);
            offset += decoded.units;
            const UINT32 codepoint = decoded.value;

            UINT16 glyph_index = 0;
            winrt::check_hresult(face->GetGlyphIndices(&codepoint, 1, &glyph_index));
            if (glyph_index == 0)
            {
                return false;
            }

            DWRITE_GLYPH_METRICS metrics{};
            winrt::check_hresult(face->GetDesignGlyphMetrics(&glyph_index, 1, &metrics, FALSE));
            glyph_indices_.push_back(glyph_index);
            advances_.push_back(size_ * static_cast<float>(metrics.advanceWidth) / units_per_em_);
        }

        return !glyph_indices_.empty();
    }

    void rasterize_cluster(std::span<const char16_t> cluster)
    {
        assert(!cluster.empty());

        IDWriteFontFace *chosen_face = nullptr;
        for (const auto &face : faces_)
        {
            if (collect_glyphs(face.get(), cluster))
            {
                chosen_face = face.get();
                break;
            }
        }

        if (chosen_face == nullptr)
        {
            (void)collect_glyphs(faces_.front().get(), cluster);
            chosen_face = faces_.front().get();
        }

        if (glyph_indices_.empty())
        {
            return;
        }

        DWRITE_GLYPH_RUN run{};
        run.fontFace = chosen_face;
        run.fontEmSize = size_;
        run.glyphCount = static_cast<UINT16>(glyph_indices_.size());
        run.glyphIndices = glyph_indices_.data();
        run.glyphAdvances = advances_.data();
        run.isSideways = FALSE;
        run.bidiLevel = 0;

        winrt::com_ptr<IDWriteGlyphRunAnalysis> analysis;
        auto factory = factory_.as<IDWriteFactory2>();
        winrt::check_hresult(factory->CreateGlyphRunAnalysis(
            &run, nullptr, DWRITE_RENDERING_MODE_NATURAL, DWRITE_MEASURING_MODE_NATURAL,
            DWRITE_GRID_FIT_MODE_DEFAULT, DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE, 0.0f, 0.0f, analysis.put()));

        RECT bounds{};
        winrt::check_hresult(analysis->GetAlphaTextureBounds(DWRITE_TEXTURE_ALIASED_1x1, &bounds));
        assert(bounds.right > bounds.left);
        assert(bounds.bottom > bounds.top);
        if (bounds.right <= bounds.left || bounds.bottom <= bounds.top)
        {
            return;
        }

        const auto width = static_cast<std::size_t>(bounds.right - bounds.left);
        const auto height = static_cast<std::size_t>(bounds.bottom - bounds.top);
        const auto offset = alpha_storage_.size();
        alpha_storage_.resize(offset + width * height);
        winrt::check_hresult(analysis->CreateAlphaTexture(
            DWRITE_TEXTURE_ALIASED_1x1, &bounds, alpha_storage_.data() + offset,
            static_cast<UINT32>(width * height)));

        regions_.push_back(glyph_region{
            .width = static_cast<int>(width),
            .height = static_cast<int>(height),
            .left = bounds.left,
            .top = bounds.top,
            .offset = offset,
            .size = width * height,
        });
    }

    winrt::com_ptr<IDWriteFactory> factory_;
    std::vector<winrt::com_ptr<IDWriteFontFace>> faces_;
    float size_{};
    float units_per_em_{};
    cell_metrics metrics_{};

    std::vector<UINT16> glyph_indices_;
    std::vector<FLOAT> advances_;
    std::vector<glyph_region> regions_;
    std::vector<std::uint8_t> alpha_storage_;
};

} // namespace corehost::uitext
