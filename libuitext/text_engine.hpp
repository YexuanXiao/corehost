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
// The family list is handed to DirectWrite's font fallback builder. DirectWrite
// chooses the font for each mapped range and also supplies its system fallback;
// this code never probes individual characters.
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

        winrt::check_hresult(factory_->GetSystemFontCollection(collection_.put(), FALSE));

        std::vector<const WCHAR *> target_names;
        target_names.reserve(families.size());
        for (const auto family : families)
        {
            assert(!family.empty());
            target_names.push_back(family.c_str());
        }

        auto factory2 = factory_.as<IDWriteFactory2>();
        winrt::com_ptr<IDWriteFontFallbackBuilder> builder;
        winrt::check_hresult(factory2->CreateFontFallbackBuilder(builder.put()));

        const DWRITE_UNICODE_RANGE all_unicode{0, 0x10FFFF};
        winrt::check_hresult(builder->AddMapping(&all_unicode, 1, target_names.data(),
                                                  static_cast<UINT32>(target_names.size()), collection_.get()));

        winrt::com_ptr<IDWriteFontFallback> system_fallback;
        winrt::check_hresult(factory2->GetSystemFontFallback(system_fallback.put()));
        winrt::check_hresult(builder->AddMappings(system_fallback.get()));
        winrt::check_hresult(builder->CreateFontFallback(fallback_.put()));

        resolve_base_face(families);
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

        analysis_source source{text};
        UINT32 position = 0;
        while (position < text.size())
        {
            UINT32 mapped_length = 0;
            FLOAT scale = 1.0f;
            winrt::com_ptr<IDWriteFont> mapped_font;
            winrt::check_hresult(fallback_->MapCharacters(
                &source, position, static_cast<UINT32>(text.size()) - position, collection_.get(), nullptr,
                DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, &mapped_length,
                mapped_font.put(), &scale));
            assert(mapped_length > 0);
            if (mapped_length == 0)
            {
                break;
            }

            winrt::com_ptr<IDWriteFontFace> face;
            if (mapped_font)
            {
                winrt::check_hresult(mapped_font->CreateFontFace(face.put()));
            }

            const auto chunk = text.substr(position, mapped_length);
            for (const auto cluster : screen::grapheme_range<screen::text_measurement::graphemes>{chunk})
            {
                rasterize_cluster(cluster.text, face ? face.get() : base_face_.get());
            }
            position += mapped_length;
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
    class analysis_source final : public winrt::implements<analysis_source, IDWriteTextAnalysisSource>
    {
      public:
        explicit analysis_source(std::u16string_view text) noexcept : text_{text}
        {
        }

        HRESULT GetTextAtPosition(UINT32 text_position, WCHAR const **text_string, UINT32 *text_length) noexcept
        {
            if (text_position >= text_.size())
            {
                *text_string = nullptr;
                *text_length = 0;
                return S_OK;
            }
            *text_string = reinterpret_cast<WCHAR const *>(text_.data() + text_position);
            *text_length = static_cast<UINT32>(text_.size() - text_position);
            return S_OK;
        }

        HRESULT GetTextBeforePosition(UINT32 text_position, WCHAR const **text_string, UINT32 *text_length) noexcept
        {
            if (text_position == 0 || text_position > text_.size())
            {
                *text_string = nullptr;
                *text_length = 0;
                return S_OK;
            }
            *text_string = reinterpret_cast<WCHAR const *>(text_.data());
            *text_length = text_position;
            return S_OK;
        }

        DWRITE_READING_DIRECTION GetParagraphReadingDirection() noexcept
        {
            return DWRITE_READING_DIRECTION_LEFT_TO_RIGHT;
        }

        HRESULT GetLocaleName(UINT32 text_position, UINT32 *text_length, WCHAR const **locale_name) noexcept
        {
            static constexpr WCHAR locale[] = L"en-us";
            *text_length = text_position < text_.size() ? static_cast<UINT32>(text_.size() - text_position) : 0;
            *locale_name = locale;
            return S_OK;
        }

        HRESULT GetNumberSubstitution(UINT32 text_position, UINT32 *text_length,
                                      IDWriteNumberSubstitution **number_substitution) noexcept
        {
            *text_length = text_position < text_.size() ? static_cast<UINT32>(text_.size() - text_position) : 0;
            *number_substitution = nullptr;
            return S_OK;
        }

      private:
        std::u16string_view text_;
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

    void resolve_base_face(std::span<const win32::wcstring_view> families)
    {
        for (const auto family : families)
        {
            UINT32 family_index = 0;
            BOOL exists = FALSE;
            winrt::check_hresult(collection_->FindFamilyName(family.c_str(), &family_index, &exists));
            if (exists == FALSE)
            {
                continue;
            }

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

    void rasterize_cluster(std::span<const char16_t> cluster, IDWriteFontFace *face)
    {
        assert(!cluster.empty());
        if (!collect_glyphs(face, cluster))
        {
            return;
        }

        DWRITE_GLYPH_RUN run{};
        run.fontFace = face;
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
    float size_{};
    float units_per_em_{};
    cell_metrics metrics_{};

    std::vector<UINT16> glyph_indices_;
    std::vector<FLOAT> advances_;
    std::vector<glyph_region> regions_;
    std::vector<std::uint8_t> alpha_storage_;
};

} // namespace corehost::uitext
