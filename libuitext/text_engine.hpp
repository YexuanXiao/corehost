#pragma once

#include "win32/string.hpp"

#include <dwrite.h>
#include <dwrite_1.h>
#include <dwrite_2.h>
#include <winrt/base.h>

#include <cassert>
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef COREHOST_FONT_MONO
#define COREHOST_FONT_MONO 0
#endif

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

enum class glyph_color_kind : std::uint8_t
{
    monochrome,
    color,
};

struct color_glyph_bitmap final
{
    std::span<const std::uint8_t> alpha;
    std::uint32_t rgb{0xFFFFFF};
    std::int16_t width{};
    std::int16_t height{};
    std::int16_t left{};
    std::int16_t top{};
    std::uint8_t alpha_scale{255};
    glyph_color_kind kind{glyph_color_kind::monochrome};
};

static_assert(sizeof(color_glyph_bitmap) <= 32);

// family is a borrowed view. The caller must keep the backing string alive
// for the entire lifetime of text_engine.
struct font_fallback_entry final
{
    win32::wcstring_view family;
    DWRITE_UNICODE_RANGE range{0, 0x10FFFF};
};

// Test-build switch: Cascadia Mono instead of Cascadia Code as the primary
// face. Both lists keep the same fallback order.
[[nodiscard]] inline std::array<font_fallback_entry, 3> default_font_families() noexcept
{
#if COREHOST_FONT_MONO
    return {font_fallback_entry{L"Cascadia Mono"}, font_fallback_entry{L"Cascadia Code"},
            font_fallback_entry{L"Consolas"}};
#else
    return {font_fallback_entry{L"Cascadia Code"}, font_fallback_entry{L"Cascadia Mono"},
            font_fallback_entry{L"Consolas"}};
#endif
}

// Header-only DirectWrite text layout and glyph-run rasterization.
//
// The family list is registered as DirectWrite's fallback chain, followed by
// the system fallback. IDWriteTextLayout then selects fonts and shapes the
// text. This code never probes fonts or characters itself.
//
// CJK fallback sizing is deliberately unresolved. Ghostty measures the
// ideograph advance ("水") per face and scales fallback font sizes to match
// the primary face's 2-cell ideograph width. Windows Terminal instead keeps
// the fallback glyph size and forces the shaped advance to 2 cells. The
// current implementation does neither; see doc/window/0009-cjk-font-metrics.md.
class text_engine final
{
  public:
    explicit text_engine(std::span<const font_fallback_entry> families, float size)
        : size_{size}, families_{families}
    {
        assert(!families_.empty());
        assert(size_ > 0.0f);
        winrt::com_ptr<IUnknown> unknown;
        winrt::check_hresult(
            ::DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), unknown.put()));
        factory2_ = unknown.as<IDWriteFactory2>();

        winrt::com_ptr<IDWriteRenderingParams> default_rendering_params;
        winrt::check_hresult(factory2_->CreateRenderingParams(default_rendering_params.put()));
        rendering_params_ = default_rendering_params.as<IDWriteRenderingParams1>();
        grayscale_enhanced_contrast_ = rendering_params_->GetGrayscaleEnhancedContrast();
        gamma_ratios_ = gamma_1_8_ratios();

        winrt::check_hresult(factory2_->GetSystemFontCollection(collection_.put(), FALSE));
        winrt::com_ptr<IDWriteFontFallbackBuilder> builder;
        winrt::check_hresult(factory2_->CreateFontFallbackBuilder(builder.put()));

        for (const auto &entry : families)
        {
            assert(!entry.family.empty());
            assert(entry.range.first <= entry.range.last);
            const WCHAR *name = entry.family.c_str();
            winrt::check_hresult(
                builder->AddMapping(&entry.range, 1, &name, 1, collection_.get()));
        }

        winrt::com_ptr<IDWriteFontFallback> system_fallback;
        winrt::check_hresult(factory2_->GetSystemFontFallback(system_fallback.put()));
        winrt::check_hresult(builder->AddMappings(system_fallback.get()));

        winrt::check_hresult(builder->CreateFontFallback(fallback_.put()));
        metrics_ = resolve_base_family(families_, fallback_.get());
    }

    text_engine(const text_engine &) = delete;
    text_engine &operator=(const text_engine &) = delete;

    [[nodiscard]] const cell_metrics &metrics() const noexcept
    {
        return metrics_;
    }

    [[nodiscard]] float grayscale_enhanced_contrast() const noexcept
    {
        return grayscale_enhanced_contrast_;
    }

    [[nodiscard]] const std::array<float, 4> &gamma_ratios() const noexcept
    {
        return gamma_ratios_;
    }

    void set_size(float size)
    {
        assert(size > 0.0f);
        if (size == size_)
        {
            return;
        }
        size_ = size;
        metrics_ = resolve_base_family(families_, fallback_.get());
    }

    void rasterize(std::u16string_view text, std::vector<glyph_bitmap> &out, bool bold = false)
    {
        out.clear();
        regions_.clear();
        alpha_storage_.clear();

        assert(!text.empty());

        winrt::com_ptr<IDWriteTextLayout> layout;
        winrt::check_hresult(factory2_->CreateTextLayout(
            reinterpret_cast<const WCHAR *>(text.data()), static_cast<UINT32>(text.size()),
            bold ? bold_format_.get() : text_format_.get(),
            100000.0f, 100000.0f, layout.put()));

        run_renderer renderer{*this, false};
        winrt::check_hresult(layout->Draw(nullptr, &renderer, 0.0f, 0.0f));

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

    void rasterize_color(std::u16string_view text, std::vector<color_glyph_bitmap> &out)
    {
        out.clear();
        color_layers_.clear();
        color_alpha_storage_.clear();

        assert(!text.empty());

        winrt::com_ptr<IDWriteTextLayout> layout;
        winrt::check_hresult(factory2_->CreateTextLayout(
            reinterpret_cast<const WCHAR *>(text.data()), static_cast<UINT32>(text.size()), text_format_.get(),
            100000.0f, 100000.0f, layout.put()));

        run_renderer renderer{*this, true};
        winrt::check_hresult(layout->Draw(nullptr, &renderer, 0.0f, 0.0f));

        out.reserve(color_layers_.size());
        for (const auto &layer : color_layers_)
        {
            out.push_back(color_glyph_bitmap{
                .width = layer.width,
                .height = layer.height,
                .left = layer.left,
                .top = layer.top,
                .rgb = layer.rgb,
                .alpha_scale = layer.alpha_scale,
                .kind = layer.kind,
                .alpha = std::span<const std::uint8_t>{color_alpha_storage_.data() + layer.offset, layer.size},
            });
        }
    }

  private:
    class run_renderer final : public winrt::implements<run_renderer, IDWriteTextRenderer>
    {
      public:
        explicit run_renderer(text_engine &owner, bool color_mode) noexcept : owner_{owner}, color_mode_{color_mode}
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

        HRESULT DrawGlyphRun(void *, FLOAT baseline_x, FLOAT baseline_y, DWRITE_MEASURING_MODE,
                             const DWRITE_GLYPH_RUN *glyph_run, const DWRITE_GLYPH_RUN_DESCRIPTION *, IUnknown *) noexcept
        {
            if (glyph_run != nullptr && glyph_run->glyphCount != 0)
            {
                if (color_mode_)
                {
                    owner_.rasterize_color_run(*glyph_run, baseline_x, baseline_y);
                }
                else
                {
                    owner_.rasterize_run(*glyph_run);
                }
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
        bool color_mode_{};
    };

    struct color_layer final
    {
        std::size_t offset{};
        std::size_t size{};
        std::uint32_t rgb{0xFFFFFF};
        std::int16_t width{};
        std::int16_t height{};
        std::int16_t left{};
        std::int16_t top{};
        std::uint8_t alpha_scale{255};
        glyph_color_kind kind{glyph_color_kind::monochrome};
    };

    static_assert(sizeof(color_layer) <= 32);

    struct glyph_region final
    {
        int width{};
        int height{};
        int left{};
        int top{};
        std::size_t offset{};
        std::size_t size{};
    };

    [[nodiscard]] cell_metrics resolve_base_family(std::span<const font_fallback_entry> families,
                                                   IDWriteFontFallback *fallback)
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

            winrt::check_hresult(factory2_->CreateTextFormat(
                family.family.c_str(), collection_.get(), DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL, size_, L"", text_format_.put()));
            winrt::check_hresult(text_format_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP));
            auto format1 = text_format_.as<IDWriteTextFormat1>();
            winrt::check_hresult(format1->SetFontFallback(fallback));

            winrt::check_hresult(factory2_->CreateTextFormat(
                family.family.c_str(), collection_.get(), DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL, size_, L"", bold_format_.put()));
            winrt::check_hresult(bold_format_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP));
            auto bold_format1 = bold_format_.as<IDWriteTextFormat1>();
            winrt::check_hresult(bold_format1->SetFontFallback(fallback));

            winrt::com_ptr<IDWriteFontFamily> font_family;
            winrt::check_hresult(collection_->GetFontFamily(family_index, font_family.put()));
            winrt::com_ptr<IDWriteFont> font;
            winrt::check_hresult(font_family->GetFirstMatchingFont(
                DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, font.put()));
            winrt::com_ptr<IDWriteFontFace> base_face;
            winrt::check_hresult(font->CreateFontFace(base_face.put()));
            return measure_face(base_face.get(), size_);
        }

        assert(false);
        winrt::throw_hresult(DWRITE_E_NOFONT);
    }

    [[nodiscard]] static std::array<float, 4> gamma_1_8_ratios() noexcept
    {
        // Default DirectWrite grayscale gamma. The shader applies the same
        // polynomial as WT's DWrite_ApplyAlphaCorrection.
        return {0.148054421f, -0.894594550f, 1.47590804f, -0.324668258f};
    }

    [[nodiscard]] static std::uint8_t color_byte(float value) noexcept
    {
        if (value <= 0.0f)
        {
            return 0;
        }
        if (value >= 1.0f)
        {
            return 255;
        }
        return static_cast<std::uint8_t>(value * 255.0f + 0.5f);
    }

    [[nodiscard]] static cell_metrics measure_face(IDWriteFontFace *face, float size)
    {
        DWRITE_FONT_METRICS font_metrics{};
        face->GetMetrics(&font_metrics);
        const auto units_per_em = static_cast<float>(font_metrics.designUnitsPerEm);
        assert(units_per_em > 0.0f);

        cell_metrics metrics{};
        metrics.ascent = size * static_cast<float>(font_metrics.ascent) / units_per_em;
        metrics.descent = size * static_cast<float>(font_metrics.descent) / units_per_em;
        metrics.line_gap = size * static_cast<float>(font_metrics.lineGap) / units_per_em;

        const UINT32 sample = u'M';
        UINT16 glyph = 0;
        winrt::check_hresult(face->GetGlyphIndices(&sample, 1, &glyph));
        DWRITE_GLYPH_METRICS glyph_metrics{};
        winrt::check_hresult(face->GetDesignGlyphMetrics(&glyph, 1, &glyph_metrics, FALSE));
        metrics.advance = size * static_cast<float>(glyph_metrics.advanceWidth) / units_per_em;
        return metrics;
    }

    // Do not use IDWriteFontFace3::GetRecommendedRenderingMode here.
    // It selected modes that distorted (warped) glyphs for corehost's
    // Cascadia font sizes, so corehost intentionally keeps NATURAL rendering
    // and GRID_FIT_DEFAULT.
    //
    // Calls must use DWRITE_RENDERING_MODE_NATURAL + DWRITE_GRID_FIT_MODE_DEFAULT.

    void rasterize_color_run(const DWRITE_GLYPH_RUN &run, float origin_x, float origin_y)
    {
        winrt::com_ptr<IDWriteColorGlyphRunEnumerator> layers;
        const auto hr = factory2_->TranslateColorGlyphRun(origin_x, origin_y, &run, nullptr,
                                                          DWRITE_MEASURING_MODE_NATURAL, nullptr, 0, layers.put());
        if (hr == DWRITE_E_NOCOLOR)
        {
            rasterize_color_layer(run, origin_x, origin_y, DWRITE_COLOR_F{1.0f, 1.0f, 1.0f, 1.0f},
                                  static_cast<UINT16>(0xFFFF));
            return;
        }
        winrt::check_hresult(hr);
        assert(layers != nullptr);

        BOOL has_run = FALSE;
        while (SUCCEEDED(layers->MoveNext(&has_run)) && has_run)
        {
            const DWRITE_COLOR_GLYPH_RUN *layer = nullptr;
            winrt::check_hresult(layers->GetCurrentRun(&layer));
            assert(layer != nullptr);
            rasterize_color_layer(layer->glyphRun, layer->baselineOriginX - origin_x,
                                  layer->baselineOriginY - origin_y, layer->runColor, layer->paletteIndex);
        }
    }

    void rasterize_color_layer(const DWRITE_GLYPH_RUN &run, float origin_x, float origin_y, DWRITE_COLOR_F color,
                               UINT16 palette_index)
    {
        winrt::com_ptr<IDWriteGlyphRunAnalysis> analysis;
        winrt::check_hresult(factory2_->CreateGlyphRunAnalysis(
            &run, nullptr, DWRITE_RENDERING_MODE_NATURAL, DWRITE_MEASURING_MODE_NATURAL,
            DWRITE_GRID_FIT_MODE_DEFAULT, DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE, origin_x, origin_y,
            analysis.put()));

        RECT bounds{};
        winrt::check_hresult(analysis->GetAlphaTextureBounds(DWRITE_TEXTURE_ALIASED_1x1, &bounds));
        if (bounds.right <= bounds.left || bounds.bottom <= bounds.top)
        {
            return;
        }

        const auto width = static_cast<std::size_t>(bounds.right - bounds.left);
        const auto height = static_cast<std::size_t>(bounds.bottom - bounds.top);
        const auto offset = color_alpha_storage_.size();
        color_alpha_storage_.resize(offset + width * height);
        winrt::check_hresult(analysis->CreateAlphaTexture(
            DWRITE_TEXTURE_ALIASED_1x1, &bounds, color_alpha_storage_.data() + offset,
            static_cast<UINT32>(width * height)));

        const auto kind = palette_index != 0xFFFF ? glyph_color_kind::color : glyph_color_kind::monochrome;
        const auto alpha_scale = kind == glyph_color_kind::color ? color_byte(color.a) : std::uint8_t{255};

        color_layers_.push_back(color_layer{
            .offset = offset,
            .size = width * height,
            .rgb = kind == glyph_color_kind::color ? (static_cast<std::uint32_t>(color_byte(color.r)) << 16 |
                              static_cast<std::uint32_t>(color_byte(color.g)) << 8 |
                              static_cast<std::uint32_t>(color_byte(color.b)))
                           : 0xFFFFFFu,
            .width = static_cast<std::int16_t>(width),
            .height = static_cast<std::int16_t>(height),
            .left = static_cast<std::int16_t>(bounds.left),
            .top = static_cast<std::int16_t>(bounds.top),
            .alpha_scale = alpha_scale,
            .kind = kind,
        });
    }

    void rasterize_run(const DWRITE_GLYPH_RUN &run, float origin_x = 0.0f, float origin_y = 0.0f)
    {
        winrt::com_ptr<IDWriteGlyphRunAnalysis> analysis;
        winrt::check_hresult(factory2_->CreateGlyphRunAnalysis(
            &run, nullptr, DWRITE_RENDERING_MODE_NATURAL, DWRITE_MEASURING_MODE_NATURAL,
            DWRITE_GRID_FIT_MODE_DEFAULT, DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE, origin_x, origin_y,
            analysis.put()));

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

    winrt::com_ptr<IDWriteFactory2> factory2_;
    winrt::com_ptr<IDWriteFontCollection> collection_;
    std::span<const font_fallback_entry> families_;
    winrt::com_ptr<IDWriteFontFallback> fallback_;
    winrt::com_ptr<IDWriteTextFormat> text_format_;
    winrt::com_ptr<IDWriteTextFormat> bold_format_;
    winrt::com_ptr<IDWriteRenderingParams1> rendering_params_;
    float grayscale_enhanced_contrast_{};
    std::array<float, 4> gamma_ratios_{};
    float size_{};
    cell_metrics metrics_{};

    std::vector<glyph_region> regions_;
    std::vector<std::uint8_t> alpha_storage_;
    std::vector<color_layer> color_layers_;
    std::vector<std::uint8_t> color_alpha_storage_;
};

} // namespace corehost::uitext
