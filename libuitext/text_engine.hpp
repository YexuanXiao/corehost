#pragma once

#include "text_writer.hpp"

#include <dwrite.h>
#include <dwrite_2.h>
#include <winrt/base.h>

#include <cassert>
#include <cstdint>
#include <span>
#include <string_view>
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
    std::vector<std::uint8_t> alpha;
};

// Header-only DirectWrite metrics and grapheme-cluster rasterization. Text
// stays UTF-16 throughout; clusters come from libscreen's grapheme range.
class text_engine final
{
  public:
    explicit text_engine(const wchar_t *family, float size) : size_{size}
    {
        assert(family != nullptr);
        assert(*family != L'\0');
        assert(size_ > 0.0f);

        winrt::com_ptr<IUnknown> unknown;
        winrt::check_hresult(
            ::DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), unknown.put()));
        factory_ = unknown.as<IDWriteFactory>();

        winrt::com_ptr<IDWriteFontCollection> collection;
        winrt::check_hresult(factory_->GetSystemFontCollection(collection.put(), FALSE));

        UINT32 family_index = 0;
        BOOL exists = FALSE;
        winrt::check_hresult(collection->FindFamilyName(family, &family_index, &exists));
        assert(exists == TRUE);
        if (exists == FALSE)
        {
            winrt::throw_hresult(DWRITE_E_NOFONT);
        }

        winrt::com_ptr<IDWriteFontFamily> font_family;
        winrt::check_hresult(collection->GetFontFamily(family_index, font_family.put()));

        winrt::com_ptr<IDWriteFont> font;
        winrt::check_hresult(font_family->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                                               DWRITE_FONT_STYLE_NORMAL, font.put()));
        winrt::check_hresult(font->CreateFontFace(font_face_.put()));

        DWRITE_FONT_METRICS font_metrics{};
        font_face_->GetMetrics(&font_metrics);
        units_per_em_ = static_cast<float>(font_metrics.designUnitsPerEm);
        assert(units_per_em_ > 0.0f);
        metrics_.ascent = size_ * static_cast<float>(font_metrics.ascent) / units_per_em_;
        metrics_.descent = size_ * static_cast<float>(font_metrics.descent) / units_per_em_;
        metrics_.line_gap = size_ * static_cast<float>(font_metrics.lineGap) / units_per_em_;

        const UINT32 sample = u'M';
        UINT16 glyph = 0;
        winrt::check_hresult(font_face_->GetGlyphIndices(&sample, 1, &glyph));
        DWRITE_GLYPH_METRICS glyph_metrics{};
        winrt::check_hresult(font_face_->GetDesignGlyphMetrics(&glyph, 1, &glyph_metrics, FALSE));
        metrics_.advance = size_ * static_cast<float>(glyph_metrics.advanceWidth) / units_per_em_;
    }

    text_engine(const text_engine &) = delete;
    text_engine &operator=(const text_engine &) = delete;

    [[nodiscard]] const cell_metrics &metrics() const noexcept
    {
        return metrics_;
    }

    [[nodiscard]] std::vector<glyph_bitmap> rasterize(std::u16string_view text) const
    {
        std::vector<glyph_bitmap> result;
        for (const auto cluster : screen::grapheme_range<screen::text_measurement::graphemes>{text})
        {
            result.push_back(rasterize_cluster(cluster.text));
        }
        return result;
    }

  private:
    [[nodiscard]] glyph_bitmap rasterize_cluster(std::span<const char16_t> cluster) const
    {
        assert(!cluster.empty());

        std::vector<UINT16> glyph_indices;
        std::vector<FLOAT> advances;
        glyph_indices.reserve(cluster.size());
        advances.reserve(cluster.size());

        const std::u16string_view view{cluster.data(), cluster.size()};
        std::size_t offset = 0;
        while (offset < view.size())
        {
            const auto decoded = screen::next_codepoint(view, offset);
            offset += decoded.units;
            const UINT32 codepoint = decoded.value;

            UINT16 glyph_index = 0;
            winrt::check_hresult(font_face_->GetGlyphIndices(&codepoint, 1, &glyph_index));
            if (glyph_index == 0)
            {
                continue;
            }

            DWRITE_GLYPH_METRICS metrics{};
            winrt::check_hresult(font_face_->GetDesignGlyphMetrics(&glyph_index, 1, &metrics, FALSE));
            glyph_indices.push_back(glyph_index);
            advances.push_back(size_ * static_cast<float>(metrics.advanceWidth) / units_per_em_);
        }

        if (glyph_indices.empty())
        {
            return {};
        }

        DWRITE_GLYPH_RUN run{};
        run.fontFace = font_face_.get();
        run.fontEmSize = size_;
        run.glyphCount = static_cast<UINT16>(glyph_indices.size());
        run.glyphIndices = glyph_indices.data();
        run.glyphAdvances = advances.data();
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
            return {};
        }

        glyph_bitmap bitmap{};
        bitmap.width = bounds.right - bounds.left;
        bitmap.height = bounds.bottom - bounds.top;
        bitmap.left = bounds.left;
        bitmap.top = bounds.top;
        bitmap.alpha.resize(static_cast<std::size_t>(bitmap.width) * static_cast<std::size_t>(bitmap.height));
        winrt::check_hresult(analysis->CreateAlphaTexture(DWRITE_TEXTURE_ALIASED_1x1, &bounds, bitmap.alpha.data(),
                                                          static_cast<UINT32>(bitmap.alpha.size())));
        return bitmap;
    }

    winrt::com_ptr<IDWriteFactory> factory_;
    winrt::com_ptr<IDWriteFontFace> font_face_;
    float size_{};
    float units_per_em_{};
    cell_metrics metrics_{};
};

} // namespace corehost::uitext
