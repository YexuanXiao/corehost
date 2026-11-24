#pragma once

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

struct font_fallback_entry final
{
    win32::wcstring_view family;
    DWRITE_UNICODE_RANGE range{0, 0x10FFFF};
};

// Header-only DirectWrite text layout and glyph-run rasterization.
//
// The family list is registered as DirectWrite's fallback chain, followed by
// the system fallback. IDWriteTextLayout then selects fonts and shapes the
// text. This code never probes fonts or characters itself.
class text_engine final
{
  public:
    explicit text_engine(std::span<const font_fallback_entry> families, float size) : size_{size}
    {
        assert(!families.empty());
        assert(size_ > 0.0f);

        winrt::com_ptr<IUnknown> unknown;
        winrt::check_hresult(
            ::DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), unknown.put()));
        factory_ = unknown.as<IDWriteFactory>();

        winrt::check_hresult(factory_->GetSystemFontCollection(collection_.put(), FALSE));

        auto factory2 = factory_.as<IDWriteFactory2>();
        winrt::com_ptr<IDWriteFontFallbackBuilder> builder;
        winrt::check_hresult(factory2->CreateFontFallbackBuilder(builder.put()));

        for (const auto &entry : families)
        {
            assert(!entry.family.empty());
            assert(entry.range.first <= entry.range.last);
            const WCHAR *name = entry.family.c_str();
            winrt::check_hresult(
                builder->AddMapping(&entry.range, 1, &name, 1, collection_.get()));
        }

        winrt::com_ptr<IDWriteFontFallback> system_fallback;
        winrt::check_hresult(factory2->GetSystemFontFallback(system_fallback.put()));
        winrt::check_hresult(builder->AddMappings(system_fallback.get()));
        winrt::check_hresult(builder->CreateFontFallback(fallback_.put()));


        resolve_base_family(families);
        metrics_ = measure_base_face();
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

        if (!text.empty())
        {
            winrt::com_ptr<IDWriteTextLayout> layout;
            winrt::check_hresult(factory_->CreateTextLayout(
                reinterpret_cast<const WCHAR *>(text.data()), static_cast<UINT32>(text.size()), text_format_.get(),
                100000.0f, 100000.0f, layout.put()));

            run_renderer renderer{*this};
            winrt::check_hresult(layout->Draw(nullptr, &renderer, 0.0f, 0.0f));
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
    class run_renderer final : public winrt::implements<run_renderer, IDWriteTextRenderer>
    {
      public:
        explicit run_renderer(text_engine &owner) noexcept : owner_{owner}
        {
        }

        HRESULT IsPixelSnappingDisabled(void *, BOOL *disabled) noexcept
        {
            *disabled = TRUE;
            return S_OK;
        }

        HRESULT GetCurrentTransform(void *, DWRITE_MATRIX *transform) noexcept
        {
            *transform = DWRITE_MATRIX{1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
            return S_OK;
        }

        HRESULT GetPixelsPerDip(void *, FLOAT *pixels_per_dip) noexcept
        {
            *pixels_per_dip = 1.0f;
            return S_OK;
        }

        HRESULT DrawGlyphRun(void *, FLOAT, FLOAT, DWRITE_MEASURING_MODE, const DWRITE_GLYPH_RUN *glyph_run,
                             const DWRITE_GLYPH_RUN_DESCRIPTION *, IUnknown *) noexcept
        {
            if (glyph_run != nullptr && glyph_run->glyphCount != 0)
            {
                owner_.rasterize_run(*glyph_run);
            }
            return S_OK;
        }

        HRESULT DrawUnderline(void *, FLOAT, FLOAT, const DWRITE_UNDERLINE *, IUnknown *) noexcept
        {
            return S_OK;
        }

        HRESULT DrawStrikethrough(void *, FLOAT, FLOAT, const DWRITE_STRIKETHROUGH *, IUnknown *) noexcept
        {
            return S_OK;
        }

        HRESULT DrawInlineObject(void *, FLOAT, FLOAT, IDWriteInlineObject *, BOOL, BOOL, IUnknown *) noexcept
        {
            return S_OK;
        }

      private:
        text_engine &owner_;
    };

    struct glyph_region final
    {
        int width{};
        int height{};
        int left{};
        int top{};
        std::size_t offset{};
        std::size_t size{};
    };

    void resolve_base_family(std::span<const font_fallback_entry> families)
    {
        for (const auto &family : families)
        {
            UINT32 family_index = 0;
            BOOL exists = FALSE;
            winrt::check_hresult(collection_->FindFamilyName(family.family.c_str(), &family_index, &exists));
            if (exists == FALSE)
            {
                continue;
            }

            base_family_ = family.family;
            winrt::check_hresult(factory_->CreateTextFormat(
                base_family_.c_str(), collection_.get(), DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL, size_, L"en-us", text_format_.put()));
            winrt::check_hresult(text_format_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP));
            auto format1 = text_format_.as<IDWriteTextFormat1>();
            winrt::check_hresult(format1->SetFontFallback(fallback_.get()));

            winrt::com_ptr<IDWriteFontFamily> font_family;
            winrt::check_hresult(collection_->GetFontFamily(family_index, font_family.put()));
            winrt::com_ptr<IDWriteFont> font;
            winrt::check_hresult(font_family->GetFirstMatchingFont(
                DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, font.put()));
            winrt::check_hresult(font->CreateFontFace(base_face_.put()));
            return;
        }

        assert(false);
        winrt::throw_hresult(DWRITE_E_NOFONT);
    }

    [[nodiscard]] cell_metrics measure_base_face()
    {
        DWRITE_FONT_METRICS font_metrics{};
        base_face_->GetMetrics(&font_metrics);
        units_per_em_ = static_cast<float>(font_metrics.designUnitsPerEm);
        assert(units_per_em_ > 0.0f);

        cell_metrics metrics{};
        metrics.ascent = size_ * static_cast<float>(font_metrics.ascent) / units_per_em_;
        metrics.descent = size_ * static_cast<float>(font_metrics.descent) / units_per_em_;
        metrics.line_gap = size_ * static_cast<float>(font_metrics.lineGap) / units_per_em_;

        const UINT32 sample = u'M';
        UINT16 glyph = 0;
        winrt::check_hresult(base_face_->GetGlyphIndices(&sample, 1, &glyph));
        DWRITE_GLYPH_METRICS glyph_metrics{};
        winrt::check_hresult(base_face_->GetDesignGlyphMetrics(&glyph, 1, &glyph_metrics, FALSE));
        metrics.advance = size_ * static_cast<float>(glyph_metrics.advanceWidth) / units_per_em_;
        return metrics;
    }

    void rasterize_run(const DWRITE_GLYPH_RUN &run)
    {
        winrt::com_ptr<IDWriteGlyphRunAnalysis> analysis;
        auto factory = factory_.as<IDWriteFactory2>();
        winrt::check_hresult(factory->CreateGlyphRunAnalysis(
            &run, nullptr, DWRITE_RENDERING_MODE_NATURAL, DWRITE_MEASURING_MODE_NATURAL,
            DWRITE_GRID_FIT_MODE_DEFAULT, DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE, 0.0f, 0.0f, analysis.put()));

        RECT bounds{};
        winrt::check_hresult(analysis->GetAlphaTextureBounds(DWRITE_TEXTURE_ALIASED_1x1, &bounds));
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
    winrt::com_ptr<IDWriteFontCollection> collection_;
    winrt::com_ptr<IDWriteFontFallback> fallback_;
    winrt::com_ptr<IDWriteFontFace> base_face_;
    winrt::com_ptr<IDWriteTextFormat> text_format_;
    win32::wcstring_view base_family_;
    float size_{};
    float units_per_em_{};
    cell_metrics metrics_{};

    std::vector<glyph_region> regions_;
    std::vector<std::uint8_t> alpha_storage_;
};

} // namespace corehost::uitext
