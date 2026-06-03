// ── conpty/char_width.hpp ──────────────────────────
// 字符宽度计算。
//
// 功能分解：
// 1. console 模式用固定 Unicode 范围模拟传统 conhost 宽度判断。
// 2. wcswidth 模式使用 ICU/libunicode，并按 ambiguous_is_wide 修正 EAW=A。
// 3. graphemes 模式先分割 grapheme cluster，再把单个 cluster 钳制到 2 列。
//
// 返回值约定：0 表示不占列，1 表示单列，2 表示双列。
#pragma once
#include <cstdint>
#include <string_view>
#include <array>
#include <numeric>
#include "../third_parties/libunicode/src/libunicode/grapheme_segmenter.h"
#ifdef COREHOST_USE_SYSTEM_ICU
#include <icu.h>
#else
#include "../third_parties/libunicode/src/libunicode/width.h"
#include "../third_parties/libunicode/src/libunicode/codepoint_properties.h"
#endif
#include "text_measurement_mode.hpp"

namespace corehost::conpty
{

// ── console 模式: 简化 CJK/全角判断 ──────────────────
// 对标原始 conhost 中的 IsGlyphFullWidth 逻辑
// 注意: 控制字符与 tab 由调用方处理
// 返回传统 Console 格子模型下单个 codepoint 的列宽。
inline int char_width_console(char32_t cp) noexcept
{
    // console 模式刻意不依赖 ICU/EAW 数据表；它提供稳定、低成本的宽度估计，
    // 用于传统 Console API 的格子模型。
    // 控制字符（含 DEL）
    if (cp < 0x20)
        return 0;
    if (cp == 0x7F)
        return 0;
    // 软连字符 (SHY)
    if (cp == 0x00AD)
        return 0;

    // ── 零宽字符 ──
    // ZWJ (U+200D), ZWNJ (U+200C), ZWSP (U+200B), BOM (U+FEFF)
    if (cp == 0x200B || cp == 0x200C || cp == 0x200D || cp == 0xFEFF)
        return 0;
    // 组合字符 (Combining Marks)
    if (cp >= 0x0300 && cp <= 0x036F)
        return 0; // Combining Diacritical Marks
    if (cp >= 0x1AB0 && cp <= 0x1AFF)
        return 0;
    if (cp >= 0x1DC0 && cp <= 0x1DFF)
        return 0;
    if (cp >= 0x20D0 && cp <= 0x20FF)
        return 0;
    if (cp >= 0xFE20 && cp <= 0xFE2F)
        return 0;
    if (cp >= 0xFE00 && cp <= 0xFE0F)
        return 0; // Variation Selectors
    if (cp >= 0xE0100 && cp <= 0xE01EF)
        return 0;

    // ── 宽字符范围（双列） ──
    // CJK 统一表意文字 (U+4E00–U+9FFF)
    if (cp >= 0x4E00 && cp <= 0x9FFF)
        return 2;
    // CJK 扩展 A (U+3400–U+4DBF)
    if (cp >= 0x3400 && cp <= 0x4DBF)
        return 2;
    // CJK 扩展 B–G (U+20000–U+3134F)
    if (cp >= 0x20000 && cp <= 0x3134F)
        return 2;
    // CJK 兼容表意文字 (U+F900–U+FAFF)
    if (cp >= 0xF900 && cp <= 0xFAFF)
        return 2;
    // CJK 兼容补充 (U+2F800–U+2FA1F)
    if (cp >= 0x2F800 && cp <= 0x2FA1F)
        return 2;
    // 全角形式 (U+FF01–U+FF60, U+FFE0–U+FFE6)
    if (cp >= 0xFF01 && cp <= 0xFF60)
        return 2;
    if (cp >= 0xFFE0 && cp <= 0xFFE6)
        return 2;
    // 日文平假名 (U+3040–U+309F)
    if (cp >= 0x3040 && cp <= 0x309F)
        return 2;
    // 日文片假名 (U+30A0–U+30FF)
    if (cp >= 0x30A0 && cp <= 0x30FF)
        return 2;
    // 日文片假名扩展 (U+31F0–U+31FF)
    if (cp >= 0x31F0 && cp <= 0x31FF)
        return 2;
    // 韩文音节 (U+AC00–U+D7AF)
    if (cp >= 0xAC00 && cp <= 0xD7AF)
        return 2;
    // 韩文兼容字母 (U+3130–U+318F)
    if (cp >= 0x3130 && cp <= 0x318F)
        return 2;
    // 中文标点 (U+3000–U+303F)
    if (cp >= 0x3000 && cp <= 0x303F)
        return 2;
    // 中日韩符号和标点 (U+3200–U+33FF)
    if (cp >= 0x3200 && cp <= 0x33FF)
        return 2;
    // 半角/全角形式补充
    if (cp >= 0x2E80 && cp <= 0x2FDF)
        return 2; // CJK 部首补充
    if (cp >= 0x31C0 && cp <= 0x31EF)
        return 2; // CJK 笔画
    // 表情符号/emoji 范围（宽字符）
    if (cp >= 0x1F000 && cp <= 0x1FFFF)
        return 2; // Emoticons, Symbols, etc.
    if (cp >= 0x2600 && cp <= 0x27BF && cp != 0x263A && cp != 0x263B)
        return 2; // Misc Symbols (部分)

    // ── 框线字符（单列） ──
    if (cp >= 0x2500 && cp <= 0x257F)
        return 1;

    return 1;
}

#ifdef COREHOST_USE_SYSTEM_ICU
// 使用系统 ICU 的 EastAsianWidth/GeneralCategory 计算单码点终端宽度。
inline int char_width_icu(char32_t cp, bool ambiguous_is_wide = false) noexcept
{
    if (cp < 0x20)
        return 0;
    if (cp == 0x7F)
        return 0;
    if (cp > 0x10FFFF)
        return 1;

    const auto icu_cp = static_cast<UChar32>(cp);
    const auto category = u_getIntPropertyValue(icu_cp, UCHAR_GENERAL_CATEGORY);
    switch (category)
    {
    case U_NON_SPACING_MARK:
    case U_ENCLOSING_MARK:
    case U_COMBINING_SPACING_MARK:
    case U_FORMAT_CHAR:
    case U_CONTROL_CHAR:
        return 0;
    default:
        break;
    }

    const auto east_asian_width = static_cast<UEastAsianWidth>(u_getIntPropertyValue(icu_cp, UCHAR_EAST_ASIAN_WIDTH));
    if (east_asian_width == U_EA_FULLWIDTH || east_asian_width == U_EA_WIDE)
        return 2;
    if (ambiguous_is_wide && east_asian_width == U_EA_AMBIGUOUS)
        return 2;
    return 1;
}
#endif

// ── wcswidth 模式: ICU/libunicode width ───────────────
// 返回 wcwidth 风格宽度；ambiguous_is_wide 会把 EAW=A 从 1 列提升为 2 列。
inline int char_width_wcswidth(char32_t cp, bool ambiguous_is_wide = false) noexcept
{
    if (cp < 0x20)
        return 0;
    if (cp == 0x7F)
        return 0;
#ifdef COREHOST_USE_SYSTEM_ICU
    return char_width_icu(cp, ambiguous_is_wide);
#else
    // ambiguous_is_wide: 对标原始 conhost --ambiguousIsWide
    // libunicode::width() 将 EAW=Ambiguous 永远计为 1
    // 需要额外查询 EAW 属性来判断
    if (ambiguous_is_wide)
    {
        auto props = unicode::codepoint_properties::get(cp);
        if (props.east_asian_width == unicode::East_Asian_Width::Ambiguous)
            return 2;
    }
    return static_cast<int>(unicode::width(cp));
#endif
}

// ── graphemes 模式: 段级别总宽度 ─────────────────────

// 返回 Unicode width 数据下单码点宽度；grapheme 分段前的 codepoint 路径使用它。
inline int char_width_unicode(char32_t cp, bool ambiguous_is_wide = false) noexcept
{
#ifdef COREHOST_USE_SYSTEM_ICU
    return char_width_icu(cp, ambiguous_is_wide);
#else
    if (ambiguous_is_wide)
    {
        auto props = unicode::codepoint_properties::get(cp);
        if (props.east_asian_width == unicode::East_Asian_Width::Ambiguous)
            return 2;
    }
    return static_cast<int>(unicode::width(cp));
#endif
}

// 计算单个 grapheme cluster 的显示宽度；返回值最多为 2 列。
inline int grapheme_cluster_width_u32(std::u32string_view cluster, bool ambiguous_is_wide = false) noexcept
{
    if (cluster.empty())
        return 0;

    // width 是 cluster 内各码点宽度累计值，最后钳制到终端单个 cluster
    // 能占据的最大 2 列。
    const auto width =
        std::transform_reduce(cluster.begin(), cluster.end(), 0, std::plus<>{}, [ambiguous_is_wide](char32_t cp) {
            // cluster 内组合标记通常为 0 宽；emoji variation selector 需要覆盖
            // 默认 width，使整个 cluster 最终按宽 emoji 显示。
            auto w = char_width_unicode(cp, ambiguous_is_wide);
            // 对标原始 CodepointWidthDetector::_graphemeNext:
            // VS16 会把 emoji presentation 强制为宽字符，最终 cluster 宽度再 clamp 到 2。
            return cp == 0xFE0F ? 2 : w;
        });
    return width > 2 ? 2 : width;
}

// 计算整段 UTF-32 文本的显示宽度；按 grapheme cluster 分段后累计。
inline int grapheme_text_width(std::u32string_view text, bool ambiguous_is_wide = false) noexcept
{
    if (text.empty())
        return 0;

    // total 是整段文本的列宽，不做 2 列钳制。
    int total = 0;
    for (auto seg = unicode::grapheme_segmenter{text}; seg; ++seg)
        total += grapheme_cluster_width_u32(*seg, ambiguous_is_wide);
    return total;
}

// ── 统一入口 ─────────────────────────────────────────

// 根据当前会话测量模式计算单码点宽度；graphemes 模式在单码点路径下近似为 Unicode width。
inline int char_width_for_mode(char32_t cp, text_measurement_mode mode, bool ambiguous_is_wide = false) noexcept
{
    switch (mode)
    {
    case text_measurement_mode::wcswidth:
        return char_width_wcswidth(cp, ambiguous_is_wide);
    case text_measurement_mode::graphemes:
        // graphemes 模式: 单码点用 width() 作为近似
        return char_width_unicode(cp, ambiguous_is_wide);
    case text_measurement_mode::console:
    default:
        return char_width_console(cp);
    }
}

// 根据当前会话测量模式计算整段文本总宽度。
inline int text_width_for_mode(std::u32string_view text, text_measurement_mode mode,
                               bool ambiguous_is_wide = false) noexcept
{
    // graphemes 模式按 cluster 分段；其他模式逐 codepoint 累加，调用方用该
    // 返回值推进 Console 光标或计算填充宽度。
    switch (mode)
    {
    case text_measurement_mode::graphemes:
        return grapheme_text_width(text, ambiguous_is_wide);
    case text_measurement_mode::wcswidth: {
        return std::transform_reduce(text.begin(), text.end(), 0, std::plus<>{}, [ambiguous_is_wide](char32_t cp) {
            return char_width_wcswidth(cp, ambiguous_is_wide);
        });
    }
    case text_measurement_mode::console:
    default: {
        return std::transform_reduce(text.begin(), text.end(), 0, std::plus<>{}, char_width_console);
    }
    }
}

} // namespace corehost::conpty
