#include "apply.hpp"
#include "vtstate.hpp"

#include <cstdio>
#include <cstdlib>

int main()
{
    namespace vtparser = corehost::vtparser;
    namespace screen = corehost::screen;

    vtparser::raw_u32_buffer raw;
    vtparser::vt_parser parser{raw};
    auto result = parser.parse(U"AB\u4E2D");
    if (result.id != vtparser::vt_message_id::continue_text)
    {
        std::fprintf(stderr, "unexpected parse result\n");
        return EXIT_FAILURE;
    }

    screen::screen_buffer buffer{10, 5};
    screen::console_state state{10, 5};
    corehost::vtstate::state_applier applier;
    applier.apply_text(result.message, buffer, state);

    if (buffer.at(0, 0).primary() != u'A' || buffer.at(1, 0).primary() != u'B')
    {
        std::fprintf(stderr, "text cells wrong\n");
        return EXIT_FAILURE;
    }
    if (!buffer.at(2, 0).is_wide() || !buffer.at(3, 0).is_continuation())
    {
        std::fprintf(stderr, "wide cells wrong\n");
        return EXIT_FAILURE;
    }
    if (state.cursor().x != 4 || state.cursor().y != 0)
    {
        std::fprintf(stderr, "cursor wrong\n");
        return EXIT_FAILURE;
    }
    if (applier.last_consumed() != 3)
    {
        std::fprintf(stderr, "consumed wrong\n");
        return EXIT_FAILURE;
    }

    parser.reset();
    result = parser.parse(U"\x1b#6");
    if (result.id != vtparser::vt_message_id::set_line_rendition)
    {
        std::fprintf(stderr, "rendition parse failed\n");
        return EXIT_FAILURE;
    }
    applier.apply_line_rendition(result.message, buffer, state);
    if (!buffer.is_double_width(0))
    {
        std::fprintf(stderr, "double width flag missing\n");
        return EXIT_FAILURE;
    }

    parser.reset();
    result = parser.parse(U"\x1b#5");
    applier.apply_line_rendition(result.message, buffer, state);
    if (buffer.is_double_width(0))
    {
        std::fprintf(stderr, "double width flag not cleared\n");
        return EXIT_FAILURE;
    }

    parser.reset();
    result = parser.parse(U"\x1b[3;5H");
    applier.apply_cursor_position(result.message, state);
    if (state.cursor().x != 4 || state.cursor().y != 2)
    {
        std::fprintf(stderr, "cursor position failed\n");
        return EXIT_FAILURE;
    }

    parser.reset();
    result = parser.parse(U"\x1b]8;id=doc;https://example.test/\x07");
    applier.apply_hyperlink(result.message, buffer, state);
    parser.reset();
    result = parser.parse(U"L");
    applier.apply_text(result.message, buffer, state);
    if (buffer.at(4, 2).hyperlink() == screen::no_cell_hyperlink)
    {
        std::fprintf(stderr, "hyperlink cell missing\n");
        return EXIT_FAILURE;
    }
    if (buffer.hyperlinks().at(buffer.at(4, 2).hyperlink()).uri != u"https://example.test/")
    {
        std::fprintf(stderr, "hyperlink uri wrong\n");
        return EXIT_FAILURE;
    }

    parser.reset();
    result = parser.parse(U"\x1b]8;;\x07");
    applier.apply_hyperlink(result.message, buffer, state);
    if (state.default_hyperlink != screen::no_cell_hyperlink)
    {
        std::fprintf(stderr, "hyperlink close failed\n");
        return EXIT_FAILURE;
    }

    parser.reset();
    result = parser.parse(U"\x1b[2K");
    applier.apply_erase_in_line(result.message, buffer, state);
    if (buffer.at(4, 2).primary() != u' ')
    {
        std::fprintf(stderr, "erase line failed\n");
        return EXIT_FAILURE;
    }

    parser.reset();
    result = parser.parse(U"\x1b[2J");
    applier.apply_erase_in_display(result.message, buffer, state);
    if (buffer.at(0, 0).primary() != u' ' || buffer.at(4, 2).primary() != u' ')
    {
        std::fprintf(stderr, "erase display failed\n");
        return EXIT_FAILURE;
    }

    parser.reset();
    result = parser.parse(U"\x1b]133;A\x07");
    (void)corehost::vtstate::apply(result.id, result.message, buffer, state, applier);
    if (!buffer.has_flag(state.cursor().y, screen::line_flag::prompt_end))
    {
        std::fprintf(stderr, "prompt mark failed\n");
        return EXIT_FAILURE;
    }

    parser.reset();
    result = parser.parse(U"\x1b]133;D;7\x07");
    (void)corehost::vtstate::apply(result.id, result.message, buffer, state, applier);
    if (!buffer.has_flag(state.cursor().y, screen::line_flag::command_end) ||
        buffer.command_exit_code(state.cursor().y) != 7)
    {
        std::fprintf(stderr, "command mark failed\n");
        return EXIT_FAILURE;
    }

    corehost::vtstate::state_applier line_applier;
    parser.reset();
    result = parser.parse(U"\n");
    if (result.id != vtparser::vt_message_id::line_feed)
    {
        std::fprintf(stderr, "line feed parse failed\n");
        return EXIT_FAILURE;
    }
    line_applier.apply_line_feed(buffer, state);
    if (state.cursor().y != 3)
    {
        std::fprintf(stderr, "line feed cursor failed\n");
        return EXIT_FAILURE;
    }

    state.set_cursor(0, 4);
    line_applier.apply_line_feed(buffer, state);
    if (state.cursor().y != 4)
    {
        std::fprintf(stderr, "line feed scroll failed\n");
        return EXIT_FAILURE;
    }

    parser.reset();
    result = parser.parse(U"Z");
    if (!corehost::vtstate::apply(result.id, result.message, buffer, state, applier))
    {
        std::fprintf(stderr, "dispatcher rejected text\n");
        return EXIT_FAILURE;
    }

    corehost::screen::screen_buffer alt_buffer{4, 2};
    corehost::screen::console_state alt_state{4, 2};
    corehost::vtstate::state_applier alt_applier;
    parser.reset();
    result = parser.parse(U"M");
    alt_applier.apply_text(result.message, alt_buffer, alt_state);

    parser.reset();
    result = parser.parse(U"\x1b[?1049h");
    (void)corehost::vtstate::apply(result.id, result.message, alt_buffer, alt_state, alt_applier);
    if (!alt_buffer.alternate_active() || alt_buffer.at(0, 0).primary() != u' ')
    {
        std::fprintf(stderr, "alternate buffer enter failed\n");
        return EXIT_FAILURE;
    }

    parser.reset();
    result = parser.parse(U"A");
    alt_applier.apply_text(result.message, alt_buffer, alt_state);

    parser.reset();
    result = parser.parse(U"\x1b[?1049l");
    (void)corehost::vtstate::apply(result.id, result.message, alt_buffer, alt_state, alt_applier);
    if (alt_buffer.alternate_active() || alt_buffer.at(0, 0).primary() != u'M')
    {
        std::fprintf(stderr, "alternate buffer exit failed\n");
        return EXIT_FAILURE;
    }

    parser.reset();
    result = parser.parse(U"\x1b[31m");
    (void)corehost::vtstate::apply(result.id, result.message, buffer, state, applier);
    if ((state.default_attributes & 0x0F) != 0x04)
    {
        std::fprintf(stderr, "sgr red failed\n");
        return EXIT_FAILURE;
    }

    parser.reset();
    result = parser.parse(U"\x1b[1;44m");
    (void)corehost::vtstate::apply(result.id, result.message, buffer, state, applier);
    if ((state.default_attributes & 0x08) == 0 || ((state.default_attributes >> 4) & 0x0F) != 0x01)
    {
        std::fprintf(stderr, "sgr bold/blue failed\n");
        return EXIT_FAILURE;
    }

    corehost::screen::screen_buffer wrap_buffer{4, 2};
    corehost::screen::console_state wrap_state{4, 2};
    parser.reset();
    result = parser.parse(U"ABCDE");
    corehost::vtstate::state_applier wrap_applier;
    wrap_applier.apply_text(result.message, wrap_buffer, wrap_state);
    if (wrap_buffer.at(0, 0).primary() != u'A' || wrap_buffer.at(3, 0).primary() != u'D' ||
        wrap_buffer.at(0, 1).primary() != u'E' ||
        !wrap_buffer.has_flag(0, screen::line_flag::wrapped))
    {
        std::fprintf(stderr, "text wrap failed\n");
        return EXIT_FAILURE;
    }

    corehost::screen::screen_buffer edit_buffer{4, 1};
    corehost::screen::console_state edit_state{4, 1};
    corehost::screen::coord edit_cursor{0, 0};
    (void)edit_buffer.write_text_row(edit_cursor, u"ABCD", {}, corehost::screen::cell_style{}, false);
    parser.reset();
    result = parser.parse(U"\x1b[2@");
    (void)corehost::vtstate::apply(result.id, result.message, edit_buffer, edit_state, applier);
    if (edit_buffer.at(0, 0).primary() != u' ' || edit_buffer.at(2, 0).primary() != u'A' ||
        edit_buffer.at(3, 0).primary() != u'B')
    {
        std::fprintf(stderr, "insert characters failed\n");
        return EXIT_FAILURE;
    }

    parser.reset();
    result = parser.parse(U"\x1b[2P");
    (void)corehost::vtstate::apply(result.id, result.message, edit_buffer, edit_state, applier);
    if (edit_buffer.at(0, 0).primary() != u'A' || edit_buffer.at(1, 0).primary() != u'B')
    {
        std::fprintf(stderr, "delete characters failed\n");
        return EXIT_FAILURE;
    }

    parser.reset();
    result = parser.parse(U"\x1b]4;1;rgb:ff/00/00\x07");
    (void)corehost::vtstate::apply(result.id, result.message, buffer, state, applier);
    if (state.color_table[1] != 0xFF0000)
    {
        std::fprintf(stderr, "palette color failed\n");
        return EXIT_FAILURE;
    }

    state.set_cursor(2, 1);
    parser.reset();
    result = parser.parse(U"\x1b" "7");
    (void)corehost::vtstate::apply(result.id, result.message, buffer, state, applier);
    state.set_cursor(0, 0);
    parser.reset();
    result = parser.parse(U"\x1b" "8");
    (void)corehost::vtstate::apply(result.id, result.message, buffer, state, applier);
    if (state.cursor().x != 2 || state.cursor().y != 1)
    {
        std::fprintf(stderr, "save/restore cursor failed x=%zu y=%zu valid=%d\n",
                     state.cursor().x, state.cursor().y, state.saved_cursor.valid);
        return EXIT_FAILURE;
    }

    parser.reset();
    result = parser.parse(U"\x1b[?25l");
    (void)corehost::vtstate::apply(result.id, result.message, buffer, state, applier);
    if (state.cursor().visible)
    {
        std::fprintf(stderr, "cursor hide failed\n");
        return EXIT_FAILURE;
    }

    corehost::screen::screen_buffer line_buffer{4, 3};
    corehost::screen::console_state line_state{4, 3};
    (void)line_buffer.write_scalar(0, 0, u'A', corehost::screen::cell_style{});
    parser.reset();
    result = parser.parse(U"\x1b[L");
    (void)corehost::vtstate::apply(result.id, result.message, line_buffer, line_state, applier);
    if (line_buffer.at(0, 0).primary() != u' ' || line_buffer.at(0, 1).primary() != u'A')
    {
        std::fprintf(stderr, "insert line failed\n");
        return EXIT_FAILURE;
    }

    parser.reset();
    result = parser.parse(U"\x1b[M");
    (void)corehost::vtstate::apply(result.id, result.message, line_buffer, line_state, applier);
    if (line_buffer.at(0, 0).primary() != u'A')
    {
        std::fprintf(stderr, "delete line failed\n");
        return EXIT_FAILURE;
    }

    state.set_cursor(2, 1);
    parser.reset();
    result = parser.parse(U"\x1b[2A");
    (void)corehost::vtstate::apply(result.id, result.message, buffer, state, applier);
    if (state.cursor().y != 0)
    {
        std::fprintf(stderr, "cursor up failed\n");
        return EXIT_FAILURE;
    }

    parser.reset();
    result = parser.parse(U"\x1b[C");
    (void)corehost::vtstate::apply(result.id, result.message, buffer, state, applier);
    if (state.cursor().x != 3)
    {
        std::fprintf(stderr, "cursor forward failed\n");
        return EXIT_FAILURE;
    }

    parser.reset();
    result = parser.parse(U"\x1b[3G");
    (void)corehost::vtstate::apply(result.id, result.message, buffer, state, applier);
    if (state.cursor().x != 2)
    {
        std::fprintf(stderr, "cursor horizontal absolute failed\n");
        return EXIT_FAILURE;
    }

    parser.reset();
    result = parser.parse(U"\x1b[4d");
    (void)corehost::vtstate::apply(result.id, result.message, buffer, state, applier);
    if (state.cursor().y != 3)
    {
        std::fprintf(stderr, "cursor vertical absolute failed\n");
        return EXIT_FAILURE;
    }

    corehost::screen::screen_buffer erase_buffer{4, 1};
    corehost::screen::console_state erase_state{4, 1};
    corehost::screen::coord erase_cursor{0, 0};
    (void)erase_buffer.write_text_row(erase_cursor, u"ABCD", {}, corehost::screen::cell_style{}, false);
    erase_state.set_cursor(1, 0);
    parser.reset();
    result = parser.parse(U"\x1b[2X");
    (void)corehost::vtstate::apply(result.id, result.message, erase_buffer, erase_state, applier);
    if (erase_buffer.at(1, 0).primary() != u' ' || erase_buffer.at(2, 0).primary() != u' ' ||
        erase_buffer.at(3, 0).primary() != u'D')
    {
        std::fprintf(stderr, "erase characters failed\n");
        return EXIT_FAILURE;
    }

    parser.reset();
    result = parser.parse(U"\x1b[1;2r");
    (void)corehost::vtstate::apply(result.id, result.message, buffer, state, applier);
    if (state.scroll_top() != 0 || state.scroll_bottom() != 1)
    {
        std::fprintf(stderr, "scroll region failed\n");
        return EXIT_FAILURE;
    }

    parser.reset();
    result = parser.parse(U"\x1b[5;3R");
    (void)corehost::vtstate::apply(result.id, result.message, buffer, state, applier);
    if (state.cursor().x != 2 || state.cursor().y != 4)
    {
        std::fprintf(stderr, "cpr response failed\n");
        return EXIT_FAILURE;
    }

    corehost::screen::screen_buffer resize_buffer{4, 2};
    corehost::screen::console_state resize_state{4, 2};
    parser.reset();
    result = parser.parse(U"\x1b[8;3;5t");
    (void)corehost::vtstate::apply(result.id, result.message, resize_buffer, resize_state, applier);
    if (resize_buffer.buffer_size() != corehost::screen::coord{5, 3} || resize_state.height() != 3)
    {
        std::fprintf(stderr, "resize window failed\n");
        return EXIT_FAILURE;
    }

    parser.reset();
    result = parser.parse(U"\x1b[4 q");
    (void)corehost::vtstate::apply(result.id, result.message, buffer, state, applier);
    if (state.cursor().style != corehost::screen::cursor_style::underline)
    {
        std::fprintf(stderr, "cursor shape failed\n");
        return EXIT_FAILURE;
    }

    parser.reset();
    result = parser.parse(U"\x1b]0;Hello\x07");
    (void)corehost::vtstate::apply(result.id, result.message, buffer, state, applier);
    if (state.title != u"Hello")
    {
        std::fprintf(stderr, "window title failed\n");
        return EXIT_FAILURE;
    }

    parser.reset();
    result = parser.parse(U"\x1b[?3h");
    (void)corehost::vtstate::apply(result.id, result.message, buffer, state, applier);
    if (state.width() != 132)
    {
        std::fprintf(stderr, "set columns 132 failed\n");
        return EXIT_FAILURE;
    }
    parser.reset();
    result = parser.parse(U"\x1b[?3l");
    (void)corehost::vtstate::apply(result.id, result.message, buffer, state, applier);
    if (state.width() != 80)
    {
        std::fprintf(stderr, "set columns 80 failed\n");
        return EXIT_FAILURE;
    }

    parser.reset();
    result = parser.parse(U"\x1b[?1000h");
    (void)corehost::vtstate::apply(result.id, result.message, buffer, state, applier);
    if (!state.private_modes.test(1000))
    {
        std::fprintf(stderr, "private mode enable failed\n");
        return EXIT_FAILURE;
    }
    parser.reset();
    result = parser.parse(U"\x1b[?1000l");
    (void)corehost::vtstate::apply(result.id, result.message, buffer, state, applier);
    if (state.private_modes.test(1000))
    {
        std::fprintf(stderr, "private mode disable failed\n");
        return EXIT_FAILURE;
    }

    corehost::screen::screen_buffer reverse_buffer{4, 3};
    corehost::screen::console_state reverse_state{4, 3};
    (void)reverse_buffer.write_scalar(0, 0, u'A', corehost::screen::cell_style{});
    parser.reset();
    result = parser.parse(U"\x1bM");
    (void)corehost::vtstate::apply(result.id, result.message, reverse_buffer, reverse_state, applier);
    if (reverse_buffer.at(0, 0).primary() != u' ' ||
        reverse_buffer.at(0, 1).primary() != u'A')
    {
        std::fprintf(stderr, "reverse index failed\n");
        return EXIT_FAILURE;
    }

    state.set_cursor(4, 0);
    parser.reset();
    result = parser.parse(U"\x1bH");
    (void)corehost::vtstate::apply(result.id, result.message, buffer, state, applier);
    state.set_cursor(0, 0);
    parser.reset();
    result = parser.parse(U"\x1b[I");
    (void)corehost::vtstate::apply(result.id, result.message, buffer, state, applier);
    if (state.cursor().x != 4)
    {
        std::fprintf(stderr, "tab stop set failed\n");
        return EXIT_FAILURE;
    }
    parser.reset();
    result = parser.parse(U"\x1b[3g");
    (void)corehost::vtstate::apply(result.id, result.message, buffer, state, applier);
    state.set_cursor(0, 0);
    parser.reset();
    result = parser.parse(U"\x1b[I");
    (void)corehost::vtstate::apply(result.id, result.message, buffer, state, applier);
    if (state.cursor().x != 0)
    {
        std::fprintf(stderr, "tab stop clear failed\n");
        return EXIT_FAILURE;
    }

    parser.reset();
    result = parser.parse(U"\x1b[?1h");
    (void)corehost::vtstate::apply(result.id, result.message, buffer, state, applier);
    if (!state.private_modes.test(1))
    {
        std::fprintf(stderr, "cursor key mode failed\n");
        return EXIT_FAILURE;
    }

    corehost::screen::screen_buffer charset_buffer{4, 2};
    corehost::screen::console_state charset_state{4, 2};
    parser.reset();
    result = parser.parse(U"\x1b(0");
    (void)corehost::vtstate::apply(result.id, result.message, charset_buffer, charset_state, applier);
    parser.reset();
    result = parser.parse(U"q");
    (void)corehost::vtstate::apply(result.id, result.message, charset_buffer, charset_state, applier);
    if (charset_buffer.at(0, 0).primary() != u'\u2500')
    {
        std::fprintf(stderr, "line drawing charset failed\n");
        return EXIT_FAILURE;
    }

    std::printf("libvtstate tests passed\n");
    return EXIT_SUCCESS;
}