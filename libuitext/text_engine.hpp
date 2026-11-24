#pragma once

#include <dwrite.h>
#include <dwrite_2.h>
#include <libunicode/grapheme_segmenter.h>
#include <winrt/base.h>

#include <cassert>
#include <cstdint>
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

// Header-only DirectWrite metrics and grapheme-cluster rasterization. The
// input is a UTF-16 string; every output bitmap corresponds to one extended
// grapheme cluster, never to a single UTF-16 code unit.
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

    [[nodiscard]] std::vector<glyph_bitmap> rasterize(std::wstring_view text) const
    {
        const auto codepoints = to_codepoints(text);
        std::vector<glyph_bitmap> result;
        if (codepoints.empty())
        {
            return result;
        }

        unicode::grapheme_segmenter segmenter{codepoints.data(), codepoints.data() + codepoints.size()};
        for (;;)
        {
            result.push_back(rasterize_cluster(*segmenter));
            if (!segmenter.codepointsAvailable())
            {
                break;
            }
            ++segmenter;
        }
        return result;
    }

  private:
    static std::vector<char32_t> to_codepoints(std::wstring_view text)
    {
        std::vector<char32_t> result;
        result.reserve(text.size());
        for (std::size_t index = 0; index < text.size(); ++index)
        {
            const auto first = static_cast<char32_t>(text[index]);
            if (first >= 0xD800 && first <= 0xDBFF && index + 1 < text.size())
            {
                const auto second = static_cast<char32_t>(text[index + 1]);
                if (second >= 0xDC00 && second <= 0xDFFF)
                {
                    result.push_back(0x10000 + ((first - 0xD800) << 10) + (second - 0xDC00));
                    ++index;
                    continue;
                }
            }
            if ((first >= 0xD800 && first <= 0xDFFF) || first > 0x10FFFF)
            {
                result.push_back(0xFFFD);
            }
            else
            {
                result.push_back(first);
            }
        }
        return result;
    }

    [[nodiscard]] glyph_bitmap rasterize_cluster(std::u32string_view cluster) const
    {
        assert(!cluster.empty());
        assert(cluster.size() <= 0xFFFF);

        std::vector<UINT16> glyph_indices(cluster.size());
        winrt::check_hresult(font_face_->GetGlyphIndices(
            reinterpret_cast<const UINT32 *>(cluster.data()), static_cast<UINT32>(cluster.size()),
            glyph_indices.data()));

        std::vector<FLOAT> advances(cluster.size());
        for (std::size_t index = 0; index < glyph_indices.size(); ++index)
        {
            DWRITE_GLYPH_METRICS metrics{};
            winrt::check_hresult(font_face_->GetDesignGlyphMetrics(&glyph_indices[index], 1, &metrics, FALSE));
            advances[index] = size_ * static_cast<float>(metrics.advanceWidth) / units_per_em_;
        }

        DWRITE_GLYPH_RUN run{};
        run.fontFace = font_face_.get();
        run.fontEmSize = size_;
        run.glyphCount = static_cast<UINT16>(glyph_indices.size());
        run.glyphIndices = glyph_indices.data();
        run.glyphAdvances = advances.data();
        run.isSideways = FALSE;
        run.bidiLevel = 0;

        DWRITE_RENDERING_MODE rendering_mode = DWRITE_RENDERING_MODE_ALIASED;
        auto face1 = font_face_.as<IDWriteFontFace1>();
        winrt::check_hresult(face1->GetRecommendedRenderingMode(
            size_, 1.0f, 1.0f, nullptr, FALSE, DWRITE_OUTLINE_THRESHOLD_ALIASED, DWRITE_MEASURING_MODE_NATURAL,
            &rendering_mode));

        winrt::com_ptr<IDWriteGlyphRunAnalysis> analysis;
        auto factory = factory_.as<IDWriteFactory2>();
        winrt::check_hresult(factory->CreateGlyphRunAnalysis(
            &run, nullptr, rendering_mode, DWRITE_MEASURING_MODE_NATURAL, DWRITE_GRID_FIT_MODE_DEFAULT,
            DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE, 0.0f, 0.0f, analysis.put()));

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
