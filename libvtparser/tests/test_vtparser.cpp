#include "kitty.hpp"
#include "simd/simd.hpp"
#include "sixel.hpp"
#include "utility/isa_features.hpp"
#include "utf8.hpp"
#include "vt_parser.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <span>

static_assert(sizeof(corehost::vtparser::vt_count_payload) == 2);
static_assert(sizeof(corehost::vtparser::vt_position_payload) == 4);
static_assert(sizeof(corehost::vtparser::vt_scroll_region_payload) == 4);
static_assert(sizeof(corehost::vtparser::vt_resize_payload) == 4);
static_assert(sizeof(corehost::vtparser::vt_palette_payload) == 6);
static_assert(sizeof(corehost::vtparser::vt_sgr_color_payload) == 4);
static_assert(sizeof(corehost::vtparser::vt_sgr_payload) == 12);
static_assert(sizeof(corehost::vtparser::vt_mouse_payload) == 8);
static_assert(sizeof(corehost::vtparser::vt_private_mode_payload) == 38);
static_assert(sizeof(corehost::vtparser::vt_win32_key_payload) == 16);

int failures = 0;

void check(bool value, const char *message)
{
    if (!value)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

void test_simd_helpers()
{
    using namespace corehost::vtparser;
    const auto active_decode = simd::select_decode_backend();
    const auto active_ground = simd::select_ground_backend();
    std::array<char8_t, 128> bytes{};
    for (std::size_t i = 0; i < bytes.size(); ++i)
        bytes[i] = static_cast<char8_t>((i % 26) + 'A');

    std::array<char16_t, 128> scalar{};
    std::array<char16_t, 128> active{};
    for (std::size_t length = 0; length <= 80; ++length)
    {
        const auto scalar_size = simd::decode_ascii_scalar(bytes.data(), length, scalar.data());
        const auto active_size = active_decode(bytes.data(), length, active.data());
        check(scalar_size == active_size, "ascii simd/scalar size");
        check(std::equal(scalar.begin(), scalar.begin() + static_cast<std::ptrdiff_t>(scalar_size), active.begin()),
              "ascii simd/scalar bytes");
    }

    for (std::size_t position = 0; position < 80; ++position)
    {
        bytes[position] = 0xE4;
        const auto scalar_size = simd::decode_ascii_scalar(bytes.data(), 80, scalar.data());
        const auto active_size = active_decode(bytes.data(), 80, active.data());
        check(scalar_size == position, "ascii simd stops at non-ascii");
        check(active_size == scalar_size, "ascii simd non-ascii size");
        check(std::equal(scalar.begin(), scalar.begin() + static_cast<std::ptrdiff_t>(scalar_size), active.begin()),
              "ascii simd non-ascii prefix");
        bytes[position] = static_cast<char8_t>((position % 26) + 'A');
    }

    std::array<char16_t, 128> text{};
    for (std::size_t i = 0; i < text.size(); ++i)
        text[i] = static_cast<char16_t>((i % 26) + 'A');

    for (std::size_t length = 0; length <= 80; ++length)
    {
        bool scalar_ascii = true;
        bool active_ascii = true;
        const auto scalar_size = simd::ground_text_scalar(text.data(), length, scalar_ascii);
        const auto active_size = active_ground(text.data(), length, active_ascii);
        check(scalar_size == active_size, "ground simd/scalar size");
        check(scalar_ascii == active_ascii, "ground simd/scalar ascii");
    }

    for (std::size_t position = 0; position < 80; ++position)
    {
        text[position] = 0x01;
        bool scalar_ascii = true;
        bool active_ascii = true;
        const auto scalar_size = simd::ground_text_scalar(text.data(), 80, scalar_ascii);
        const auto active_size = active_ground(text.data(), 80, active_ascii);
        check(scalar_size == position, "ground simd stops at control");
        check(active_size == scalar_size, "ground simd control size");
        check(scalar_ascii == active_ascii, "ground simd control ascii");
        text[position] = static_cast<char16_t>((position % 26) + 'A');
    }

    text[5] = 0x4E2D;
    text[6] = 0x01;
    text[7] = 0x4E2D;
    bool mixed_scalar_ascii = true;
    bool mixed_active_ascii = true;
    const auto mixed_scalar_size = simd::ground_text_scalar(text.data(), 16, mixed_scalar_ascii);
    const auto mixed_active_size = active_ground(text.data(), 16, mixed_active_ascii);
    check(mixed_scalar_size == 6, "ground mixed control position");
    check(mixed_active_size == mixed_scalar_size, "ground mixed control size");
    check(!mixed_scalar_ascii && !mixed_active_ascii, "ground mixed control ascii");
    text[5] = static_cast<char16_t>((5 % 26) + 'A');
    text[6] = static_cast<char16_t>((6 % 26) + 'A');
    text[7] = static_cast<char16_t>((7 % 26) + 'A');

    const auto compare_decode = [&](auto backend) {
        for (std::size_t length = 0; length <= 80; ++length)
        {
            std::fill(scalar.begin(), scalar.end(), char16_t{});
            std::fill(active.begin(), active.end(), char16_t{});
            const auto scalar_size = simd::decode_ascii_scalar(bytes.data(), length, scalar.data());
            const auto active_size = backend(bytes.data(), length, active.data());
            check(scalar_size == active_size, "ascii backend size");
            check(std::equal(scalar.begin(), scalar.begin() + static_cast<std::ptrdiff_t>(scalar_size), active.begin()),
                  "ascii backend bytes");
        }
    };

    const auto compare_ground = [&](auto backend) {
        for (std::size_t length = 0; length <= 80; ++length)
        {
            bool scalar_ascii = true;
            bool active_ascii = true;
            const auto scalar_size = simd::ground_text_scalar(text.data(), length, scalar_ascii);
            const auto active_size = backend(text.data(), length, active_ascii);
            check(scalar_size == active_size, "ground backend size");
            check(scalar_ascii == active_ascii, "ground backend ascii");
        }
    };

#if COREHOST_X64_SIMD
    if (corehost::isa::detect_level() >= corehost::isa::level::sse42)
    {
        compare_decode(&simd::decode_ascii_sse42);
        compare_ground(&simd::ground_text_sse42);
    }
#endif
#if COREHOST_X64_SIMD
    if (corehost::isa::detect_level() >= corehost::isa::level::avx2)
    {
        compare_decode(&simd::decode_ascii_avx2);
        compare_ground(&simd::ground_text_avx2);
    }
#endif
#if COREHOST_X64_SIMD
    if (corehost::isa::detect_level() >= corehost::isa::level::avx10_1)
    {
        compare_decode(&simd::decode_ascii_avx10);
        compare_ground(&simd::ground_text_avx10);
    }
#endif
}

int main()
{
    using namespace corehost::vtparser;

    raw_u16buffer raw;
    vt_parser parser{raw};
    auto result = parser.parse(u"abc");
    check(result.id == vt_message_id::continue_text, "ground text id");
    check(result.consumed == 3, "ground text consumed");
    check(result.message.payload.text == u"abc", "ground text payload");

    parser.reset();
    result = parser.parse(u"ABCDEFG\x01");
    check(result.id == vt_message_id::continue_text && result.consumed == 7, "ground text c0 boundary");

    parser.reset();
    result = parser.parse(u"ABCDEFG\x7F");
    check(result.id == vt_message_id::continue_text && result.consumed == 7, "ground text del boundary");

    parser.reset();
    result = parser.parse(u"ABCDEFG\u4E2D");
    check(result.id == vt_message_id::continue_text && result.consumed == 8, "ground text non-ascii run");

    parser.reset();
    result = parser.parse(u"ABCD");
    check(result.id == vt_message_id::continue_text && result.message.text_is_ascii, "ground text ascii flag");

    parser.reset();
    result = parser.parse(u"abc");
    check(result.id == vt_message_id::continue_text && result.message.text_is_ascii, "short text ascii flag");

    parser.reset();
    result = parser.parse(u"AB\u4E2D");
    check(result.id == vt_message_id::continue_text && !result.message.text_is_ascii, "ground text non-ascii flag");

    parser.reset();
    result = parser.parse(u"\x1b]2;corehost\x07");
    check(result.id == vt_message_id::set_window_title, "title id");
    check(result.message.payload.title == u"corehost", "title payload");

    parser.reset();
    result = parser.parse(u"\x1b[31m");
    check(result.id == vt_message_id::sgr, "sgr id");
    check(result.consumed == 5, "sgr consumed");

    parser.reset();
    result = parser.parse(u"\x1b[?1049h");
    check(result.id == vt_message_id::use_alternate_buffer, "alternate buffer id");

    parser.reset();
    result = parser.parse(u"\x1b]8;id=link1:group;https://example.test/\x07");
    check(result.id == vt_message_id::osc8_hyperlink, "hyperlink id");
    check(result.message.payload.hyperlink.id == u"link1", "hyperlink params id");
    check(result.message.payload.hyperlink.uri == u"https://example.test/", "hyperlink uri");

    parser.reset();
    result = parser.parse(u"\x1b]8;;\x07");
    check(result.id == vt_message_id::osc8_hyperlink, "hyperlink close id");
    check(result.message.payload.hyperlink.uri.empty(), "hyperlink close uri");

    parser.reset();
    result = parser.parse(u"\x1b#3");
    check(result.id == vt_message_id::set_line_rendition, "line rendition id");
    check(result.message.payload.line_rendition == 3, "line rendition payload");

    parser.reset();
    result = parser.parse(u"\x1b]133;D;0\x07");
    check(result.id == vt_message_id::semantic_prompt_mark, "semantic prompt id");
    check(result.message.payload.semantic.kind == u'D', "semantic prompt kind");
    check(result.message.payload.semantic.argument == u"0", "semantic prompt argument");

    parser.reset();
    result = parser.parse(u"\x1b_Gi=1;AAAA\x1b\\");
    check(result.id == vt_message_id::apc_sequence, "apc id");
    check(result.message.payload.text.starts_with(u"\x1b_G"), "apc payload");

    parser.reset();
    result = parser.parse(u"\x1bPqabc\x1b\\");
    check(result.id == vt_message_id::dcs_sequence, "dcs id");
    check(result.message.payload.text.starts_with(u"\x1bP"), "dcs payload");

    const auto image = parse_kitty_graphics(u"\x1b_Ga=T,f=100,s=2,v=3,i=7;AAAA\x1b\\");
    check(image.has_value(), "kitty image parsed");
    if (image.has_value())
    {
        check(image->action == u'T', "kitty action");
        check(image->image_id == 7, "kitty id");
        check(image->width == 2 && image->height == 3, "kitty dimensions");
        check(image->payload == u"AAAA", "kitty payload");
    }

    const auto sixel = parse_sixel(u"\x1bP0;1qabc\x1b\\");
    check(sixel.has_value(), "sixel parsed");
    if (sixel.has_value())
    {
        check(sixel->parameters == u"0;1", "sixel parameters");
        check(sixel->data == u"abc", "sixel data");
    }

    std::u16string decoded;
    utf8_decoder decoder;
    constexpr char8_t utf8_text[]{0x41, 0xE4, 0xB8, 0xAD};
    decoder.decode(utf8_text, decoded);
    check(decoded == u"A\u4E2D", "utf8 decode");
    check(decoder.pending_size() == 0, "utf8 pending empty");

    decoder.decode(std::span<const char8_t>{utf8_text + 1, 1}, decoded);
    decoder.decode(std::span<const char8_t>{utf8_text + 2, 2}, decoded);
    check(decoded == u"\u4E2D", "utf8 split decode");

    constexpr char8_t emoji[]{0xF0, 0x9F, 0x98, 0x80};
    for (std::size_t i = 0; i < std::size(emoji); ++i)
    {
        decoder.decode(std::span<const char8_t>{emoji + i, 1}, decoded);
    }
    check(decoded == u"\U0001F600", "utf8 four-byte split decode");

    constexpr char8_t invalid[]{0x80};
    decoder.decode(invalid, decoded);
    check(decoded == u"\uFFFD", "utf8 invalid replacement");

    test_simd_helpers();

    if (failures != 0)
    {
        std::fprintf(stderr, "%d failures\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("libvtparser tests passed\n");
    return EXIT_SUCCESS;
}