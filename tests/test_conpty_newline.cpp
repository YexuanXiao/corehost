// === tests/test_conpty_newline.cpp ===
// Reproduces the "welcome message all on first line" bug.
// Tests that multiple WriteConsole calls with \r\n properly advance cursor.
#include "test_common.hpp"
#include "conpty/conpty_vt_parser.hpp"
#include "conpty/vt_msg_dispatch.hpp"
#include "conpty/console_state.hpp"
#include "conpty/screen_buffer.hpp"
#include <cstdio>

using namespace conpty;

void dump_sb(screen_buffer &sb, int rows)
{
    for (int y = 0; y < rows; ++y)
    {
        fprintf(stderr, "  row %d: '", y);
        for (int x = 0; x < 20; ++x)
        {
            char32_t ch = sb.at_u32({(SHORT)x, (SHORT)y});
            if (ch >= 32 && ch < 127)
                fprintf(stderr, "%c", (char)ch);
            else if (ch == U' ')
                fprintf(stderr, ".");
            else
                fprintf(stderr, "?");
        }
        fprintf(stderr, "'\n");
    }
}

// Simulate api_write_console: CUP→SGR→text (split at \\r \\n) → CUP
void sim_write_console(console_state &st, screen_buffer &sb, std::u32string_view text)
{
    // Step 1: CUP to start position
    vt_message m{};
    m.row = st.cursor.position.Y + 1;
    m.col = st.cursor.position.X + 1;
    vt_msg_apply_state(vt_message_id::cursor_position, m, st, sb);

    // Step 2: SGR (simplified — just ensure default attrs)
    m = vt_message{};
    m.sgr_reset = true;
    vt_msg_apply_state(vt_message_id::sgr, m, st, sb);

    // Step 3: text — split at \\r \\n into dedicated messages
    m = vt_message{};
    size_t seg_start = 0;
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == U'\r')
        {
            if (i > seg_start)
            {
                m.text = text.substr(seg_start, i - seg_start);
                vt_msg_apply_state(vt_message_id::text, m, st, sb);
            }
            vt_msg_apply_state(vt_message_id::carriage_return, m, st, sb);
            seg_start = i + 1;
        }
        else if (text[i] == U'\n')
        {
            if (i > seg_start)
            {
                m.text = text.substr(seg_start, i - seg_start);
                vt_msg_apply_state(vt_message_id::text, m, st, sb);
            }
            vt_msg_apply_state(vt_message_id::line_feed, m, st, sb);
            seg_start = i + 1;
        }
    }
    if (seg_start < text.size())
    {
        m.text = text.substr(seg_start);
        vt_msg_apply_state(vt_message_id::text, m, st, sb);
    }

    // Step 4: CUP to final position
    m = vt_message{};
    m.row = st.cursor.position.Y + 1;
    m.col = st.cursor.position.X + 1;
    vt_msg_apply_state(vt_message_id::cursor_position, m, st, sb);
}

bool test_two_lines()
{
    console_state st;
    screen_buffer sb({80, 25});
    st.screen_buffer_size = {80, 25};
    st.cursor.position = {0, 0};

    // Simulate cmd.exe welcome message: two WriteConsole calls
    sim_write_console(st, sb, U"Line 1: Hello World\r\n");
    fprintf(stderr, "After call 1: cursor=(%d,%d)\n", st.cursor.position.X, st.cursor.position.Y);

    sim_write_console(st, sb, U"Line 2: Goodbye\r\n");
    fprintf(stderr, "After call 2: cursor=(%d,%d)\n", st.cursor.position.X, st.cursor.position.Y);

    dump_sb(sb, 3);

    // After 2 calls: line 1 on row 0, line 2 on row 1
    ASSERT(sb.at_u32({0, 0}) == U'L'); // "Line 1"
    ASSERT(sb.at_u32({0, 1}) == U'L'); // "Line 2"
    ASSERT(st.cursor.position.Y == 2); // cursor on row 2 (0-based)
    return true;
}

bool test_cmd_welcome()
{
    console_state st;
    screen_buffer sb({80, 25});
    st.screen_buffer_size = {80, 25};
    st.cursor.position = {0, 0};

    // Simulate full cmd.exe welcome message
    sim_write_console(st, sb, U"Microsoft Windows [Version 10.0.26100.0]\r\n");
    sim_write_console(st, sb, U"(c) Microsoft Corporation. All rights reserved.\r\n");
    sim_write_console(st, sb, U"\r\n"); // blank line
    sim_write_console(st, sb, U"C:\\Users\\xyx>");

    fprintf(stderr, "Final cursor: (%d,%d)\n", st.cursor.position.X, st.cursor.position.Y);
    dump_sb(sb, 5);

    // Line 0: "Microsoft Windows..."
    ASSERT(sb.at_u32({0, 0}) == U'M');
    // Line 1: "(c) Microsoft..."
    ASSERT(sb.at_u32({0, 1}) == U'(');
    // Line 2: blank (spaces)
    ASSERT(sb.at_u32({0, 2}) == U' ');
    // Line 3: "C:\Users\xyx>"
    ASSERT(sb.at_u32({0, 3}) == U'C');
    // Cursor should be on row 3, at end of prompt
    ASSERT(st.cursor.position.Y == 3);
    ASSERT(st.cursor.position.X >= 13); // after "C:\Users\xyx>"
    return true;
}

bool test_single_call_multiline()
{
    console_state st;
    screen_buffer sb({80, 25});
    st.screen_buffer_size = {80, 25};
    st.cursor.position = {0, 0};

    // All in ONE WriteConsole call (cmd may buffer)
    sim_write_console(st, sb, U"Line A\r\nLine B\r\nLine C");

    fprintf(stderr, "Cursor: (%d,%d)\n", st.cursor.position.X, st.cursor.position.Y);
    dump_sb(sb, 4);

    ASSERT(sb.at_u32({0, 0}) == U'L'); // "Line A" on row 0
    ASSERT(sb.at_u32({0, 1}) == U'L'); // "Line B" on row 1
    ASSERT(sb.at_u32({0, 2}) == U'L'); // "Line C" on row 2
    ASSERT(st.cursor.position.Y == 2); // cursor still on row 2 (no trailing \n)
    ASSERT(st.cursor.position.X == 6); // after "Line C"
    return true;
}

// ============================================================================
// cls regression tests (Bug: cls cleared screen on terminal but new prompt
// appeared on row 1 instead of row 0, because state cursor wasn't synced)
// ============================================================================

// Simulate cmd.exe's cls: ScrollConsoleScreenBuffer full-screen + SetCursorPos(0,0)
// This mirrors the real api_scroll_sb handler flow.
void sim_cls(console_state &st, screen_buffer &sb)
{
    // cmd.exe calls ScrollConsoleScreenBuffer with:
    //   sr = {0, 0, width-1, height-1}
    //   dest = {0, -height}  (scroll entire screen up, fill with spaces)
    SHORT w = st.screen_buffer_size.X;
    SHORT h = st.screen_buffer_size.Y;
    SMALL_RECT sr{0, 0, static_cast<SHORT>(w - 1), static_cast<SHORT>(h - 1)};
    COORD dest{0, static_cast<SHORT>(-h)};
    SMALL_RECT clip{0, 0, 0, 0}; // no clip

    // This is what api_scroll_sb does:
    sb.scroll(sr, clip, false, dest, U' ', st.default_attributes);

    // Then cmd calls SetConsoleCursorPosition(0,0)
    st.cursor.position = {0, 0};
}

bool test_cls_full_screen()
{
    console_state st;
    screen_buffer sb({120, 30});
    st.screen_buffer_size = {120, 30};
    st.cursor.position = {0, 0};

    // Step 1: cmd welcome → fills rows 0-2, prompt on row 3
    sim_write_console(st, sb, U"Microsoft Windows [Version 10.0.26100.0]\r\n");
    sim_write_console(st, sb, U"(c) Microsoft Corporation. All rights reserved.\r\n");
    sim_write_console(st, sb, U"\r\n");
    sim_write_console(st, sb, U"C:\\Users\\xyx>");
    ASSERT(st.cursor.position.Y == 3);
    ASSERT(sb.at_u32({0, 0}) == U'M');

    // Step 2: cls
    sim_cls(st, sb);

    // After cls: all rows should be spaces, cursor at (0,0)
    ASSERT(st.cursor.position.X == 0);
    ASSERT(st.cursor.position.Y == 0);
    ASSERT(sb.at_u32({0, 0}) == U' ');
    ASSERT(sb.at_u32({0, 1}) == U' ');
    ASSERT(sb.at_u32({0, 2}) == U' ');
    ASSERT(sb.at_u32({0, 3}) == U' ');

    // Step 3: new prompt should appear on row 0
    sim_write_console(st, sb, U"C:\\Users\\xyx>");
    ASSERT(sb.at_u32({0, 0}) == U'C');
    ASSERT(st.cursor.position.Y == 0); // on row 0, not row 1!
    ASSERT(st.cursor.position.X >= 13);
    return true;
}

bool test_cls_multiple()
{
    console_state st;
    screen_buffer sb({80, 25});
    st.screen_buffer_size = {80, 25};
    st.cursor.position = {0, 0};

    // Fill several lines
    sim_write_console(st, sb, U"Line 1: data\r\n");
    sim_write_console(st, sb, U"Line 2: data\r\n");
    sim_write_console(st, sb, U"Line 3: data\r\n");
    sim_write_console(st, sb, U"prompt>");
    ASSERT(st.cursor.position.Y == 3);

    // First cls
    sim_cls(st, sb);
    ASSERT(st.cursor.position.X == 0);
    ASSERT(st.cursor.position.Y == 0);
    sim_write_console(st, sb, U"prompt>");
    ASSERT(sb.at_u32({0, 0}) == U'p');
    ASSERT(st.cursor.position.Y == 0);

    // Second cls
    sim_cls(st, sb);
    ASSERT(st.cursor.position.X == 0);
    ASSERT(st.cursor.position.Y == 0);
    sim_write_console(st, sb, U"prompt2>");
    ASSERT(sb.at_u32({0, 0}) == U'p');
    ASSERT(st.cursor.position.Y == 0);
    return true;
}

bool test_cls_then_echo()
{
    // Full scenario: welcome → cls → prompt → user types echo hello
    console_state st;
    screen_buffer sb({80, 25});
    st.screen_buffer_size = {80, 25};
    st.cursor.position = {0, 0};

    // Welcome
    sim_write_console(st, sb, U"Windows\r\n(c) Microsoft\r\n\r\nC:\\>");
    ASSERT(st.cursor.position.Y == 3);

    // cls
    sim_cls(st, sb);
    ASSERT(st.cursor.position.Y == 0);

    // New prompt
    sim_write_console(st, sb, U"C:\\>");
    ASSERT(st.cursor.position.Y == 0);
    ASSERT(sb.at_u32({0, 0}) == U'C');

    // echo hello output (cmd prints "hello\r\n" starting from prompt end)
    // cursor is at (3,0) after "C:\>"
    sim_write_console(st, sb, U"\r\nhello\r\n");
    // After \r\nhello\r\n: cursor moves to next line
    ASSERT(st.cursor.position.Y == 2);
    ASSERT(sb.at_u32({0, 1}) == U'h'); // "hello" on row 1

    // New prompt on row 2
    sim_write_console(st, sb, U"C:\\>");
    ASSERT(st.cursor.position.Y == 2);
    ASSERT(sb.at_u32({0, 2}) == U'C');
    return true;
}

bool test_cls_cursor_at_zero_after_echo()
{
    // Reg: after echo + cls, next prompt must be at row 0
    // This tests the exact "echo hello → cls" scenario
    console_state st;
    screen_buffer sb({80, 25});
    st.screen_buffer_size = {80, 25};
    st.cursor.position = {0, 0};

    // Welcome + prompt
    sim_write_console(st, sb, U"Windows\r\n\r\nC:\\Users\\xyx>");
    // User types echo hello → prompt advances
    sim_write_console(st, sb, U"echo hello\r\n");
    sim_write_console(st, sb, U"hello\r\n"); // cmd's output
    sim_write_console(st, sb, U"\r\n");
    sim_write_console(st, sb, U"C:\\Users\\xyx>"); // next prompt

    SHORT prompt_row = st.cursor.position.Y;
    ASSERT(prompt_row >= 3);

    // Now cls
    sim_cls(st, sb);
    ASSERT(st.cursor.position.X == 0);
    ASSERT(st.cursor.position.Y == 0);

    // After cls, prompt must appear at row 0
    sim_write_console(st, sb, U"C:\\Users\\xyx>");
    ASSERT(sb.at_u32({0, 0}) == U'C');
    ASSERT(st.cursor.position.Y == 0);
    return true;
}

int main()
{
    std::wcout << L"=== ConPTY Newline Tracking Tests ===" << std::endl;

    RUN_TEST(test_two_lines, L"Two lines");
    RUN_TEST(test_cmd_welcome, L"Cmd welcome");
    RUN_TEST(test_single_call_multiline, L"Single call multiline");

    std::wcout << L"\ncls Regression Tests:\n";
    RUN_TEST(test_cls_full_screen, L"cls full screen");
    RUN_TEST(test_cls_multiple, L"cls multiple");
    RUN_TEST(test_cls_then_echo, L"cls then echo hello");
    RUN_TEST(test_cls_cursor_at_zero_after_echo, L"cls cursor at zero after echo");

    std::wcout << L"  " << tests_passed << L" passed, " << tests_failed << L" failed, " << (tests_passed + tests_failed)
               << L" total." << std::endl;
    return tests_failed > 0 ? 1 : 0;
}
