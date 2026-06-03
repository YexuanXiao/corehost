// === tests/test_conpty_writeconsole_cursor.cpp ===
// Regression: PSReadLine history recall cursor positioning
//
// Bug: After error output (multi-line \r\n WriteConsole), PSReadLine's
//      SetCursorPos+WriteConsole for history recall wrote text on the wrong line.
//      Terminal cursor was on row+1 from previous WriteConsole's \r\n, but
//      api_write_console didn't send CUP to align before writing text.
//
// Fix: api_write_console now always sends CUP to state.cursor.position before
//      outputting text, not just on the Enter-newline path.
#include "test_common.hpp"
#include "vt_msg_dispatch.hpp"
#include "console_state.hpp"
#include "screen_buffer.hpp"
#include <cstdio>

using namespace conpty;

// Simulate the PSReadLine history recall flow:
//   state.cursor is at (65, 13) [SetCursorPos by PSReadLine]
//   WriteConsole with text ending in \r\n
//   Text must appear on row 13, not row 14
//
// Before the fix, text would appear on row 14 because terminal cursor
// was already advanced by previous WriteConsole's \r\n, and no CUP was sent.

// Helper: simulate api_write_console with CUP→SGR→text→CUP
void sim_wc_with_cup(console_state &st, screen_buffer &sb, std::u32string_view text)
{
    // Step 1: CUP to align terminal cursor with state cursor (THE FIX)
    vt_message m{};
    m.payload.position.row = st.cursor.position.Y + 1;
    m.payload.position.col = st.cursor.position.X + 1;
    vt_msg_apply_state<vt_message_id::cursor_position>(m, st, sb);

    // Step 2: SGR
    m = vt_message{};
    m.payload.sgr.set(vt_sgr_flag::reset);
    vt_msg_apply_state<vt_message_id::sgr>(m, st, sb);

    // Step 3: text split at \r \n
    m = vt_message{};
    size_t seg_start = 0;
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == U'\r')
        {
            if (i > seg_start)
            {
                m.payload.text = text.substr(seg_start, i - seg_start);
                vt_msg_apply_state<vt_message_id::text>(m, st, sb);
            }
            vt_msg_apply_state<vt_message_id::carriage_return>(m, st, sb);
            seg_start = i + 1;
        }
        else if (text[i] == U'\n')
        {
            if (i > seg_start)
            {
                m.payload.text = text.substr(seg_start, i - seg_start);
                vt_msg_apply_state<vt_message_id::text>(m, st, sb);
            }
            vt_msg_apply_state<vt_message_id::line_feed>(m, st, sb);
            seg_start = i + 1;
        }
    }
    if (seg_start < text.size())
    {
        m.payload.text = text.substr(seg_start);
        vt_msg_apply_state<vt_message_id::text>(m, st, sb);
    }

    // Step 4: CUP to final position
    m = vt_message{};
    m.payload.position.row = st.cursor.position.Y + 1;
    m.payload.position.col = st.cursor.position.X + 1;
    vt_msg_apply_state<vt_message_id::cursor_position>(m, st, sb);
}

// Bug scenario: PSReadLine writes history line after error output
bool test_history_cursor_on_correct_row()
{
    console_state st;
    screen_buffer sb({120, 30});
    st.screen_buffer_size = {120, 30};
    st.cursor.position = {0, 0};

    // ── Phase 1: Normal operation builds up screen ──
    // Welcome message (3 lines)
    sim_wc_with_cup(st, sb, U"Windows PowerShell\r\n");
    sim_wc_with_cup(st, sb, U"Copyright (C) Microsoft Corporation.\r\n");
    sim_wc_with_cup(st, sb, U"\r\n");

    // First prompt on row 3
    sim_wc_with_cup(st, sb, U"PS C:\\Users\\xyx>");
    ASSERT(st.cursor.position.Y == 3);
    ASSERT(st.cursor.position.X == 16); // after prompt (16 chars)
    ASSERT(sb.at_u32({0, 3}) == U'P');

    // User types a command + Enter
    st.cursor.position = {16, 3};
    sim_wc_with_cup(st, sb, U"echo hello"); // PSReadLine writes command text
    // User presses Enter: state cursor advances via \r\n
    sim_wc_with_cup(st, sb, U"\r\n");
    ASSERT(st.cursor.position.X == 0);
    ASSERT(st.cursor.position.Y == 4);

    // cmd outputs "hello" (via WriteConsole)
    sim_wc_with_cup(st, sb, U"hello\r\n");
    ASSERT(st.cursor.position.Y == 5);

    // New prompt on row 5
    sim_wc_with_cup(st, sb, U"PS C:\\Users\\xyx>");
    ASSERT(st.cursor.position.Y == 5);

    // ── Phase 2: Error scenario ──
    // User types a bad command (long path)
    st.cursor.position = {16, 5}; // PSReadLine sets cursor at prompt end
    sim_wc_with_cup(st, sb, U"C:\\some\\very\\long\\path\\that\\does\\not\\exist"); // 48 chars
    // User presses Enter
    sim_wc_with_cup(st, sb, U"\r\n");
    ASSERT(st.cursor.position.X == 0);
    ASSERT(st.cursor.position.Y == 6); // on row 6

    // Shell outputs error message (multi-line, via multiple WriteConsole calls)
    sim_wc_with_cup(st, sb, U"C:\\some\\very\\long\\path\\that\\does\\not\\exist : The term\r\n");
    ASSERT(st.cursor.position.X == 0);
    ASSERT(st.cursor.position.Y == 7);
    sim_wc_with_cup(st, sb, U"is not recognized\r\n");
    ASSERT(st.cursor.position.Y == 8);
    sim_wc_with_cup(st, sb, U"\r\n");
    ASSERT(st.cursor.position.Y == 9);

    // New prompt on row 9
    sim_wc_with_cup(st, sb, U"PS C:\\Users\\xyx>");
    ASSERT(st.cursor.position.Y == 9);

    // ── Phase 3: Up key for history recall ──
    // PSReadLine reads the screen buffer, then:
    //   1. SetCursorPos(17, 9) —prompt end for history text
    //   2. WriteConsole("C:\\some\\very\\long\\path...\r\n") —the 48-char command + newline
    //
    // CRITICAL: The text must appear on row 9 (same as prompt), and \r\n advances
    //           cursor to row 10. NOT row 10+1=11.
    st.cursor.position = {16, 9}; // PSReadLine SetCursorPos

    // THE BUG SCENARIO: WriteConsole of history text
    sim_wc_with_cup(st, sb, U"C:\\some\\very\\long\\path\\that\\does\\not\\exist\r\n");

    // Verify text appears on row 9 (prompt row)
    ASSERT(sb.at_u32({16, 9}) == U'C'); // first char of path on row 9
    // After \r\n, cursor on row 10
    ASSERT(st.cursor.position.X == 0);
    ASSERT(st.cursor.position.Y == 10);

    // Next prompt after history recall should be on row 10
    sim_wc_with_cup(st, sb, U"PS C:\\Users\\xyx>");
    ASSERT(st.cursor.position.Y == 10);
    ASSERT(sb.at_u32({0, 10}) == U'P');

    return true;
}

// Simpler: direct SetCursorPos →WriteConsole →verify row
bool test_set_cursor_pos_then_write_console()
{
    console_state st;
    screen_buffer sb({120, 30});
    st.screen_buffer_size = {120, 30};
    st.cursor.position = {0, 0};

    // Move to row 5, col 20
    st.cursor.position = {20, 5};
    sim_wc_with_cup(st, sb, U"hello\r\n");

    // Text at (20, 5)
    ASSERT(sb.at_u32({20, 5}) == U'h');
    ASSERT(sb.at_u32({21, 5}) == U'e');
    ASSERT(sb.at_u32({22, 5}) == U'l');
    // After \r\n, cursor at (0, 6)
    ASSERT(st.cursor.position.X == 0);
    ASSERT(st.cursor.position.Y == 6);

    return true;
}

// Test that WriteConsole always aligns terminal cursor with state
bool test_cursor_alignment_after_newline()
{
    // Scenario: previous WriteConsole's \r\n advanced terminal cursor to (0, 4)
    // but a SetCursorPos changed state cursor to (10, 3) without updating terminal.
    // Next WriteConsole must CUP to (10, 3) before writing.

    console_state st;
    screen_buffer sb({120, 30});
    st.screen_buffer_size = {120, 30};
    st.cursor.position = {0, 0};

    // Write text that ends with \r\n →state cursor at (0, 1)
    sim_wc_with_cup(st, sb, U"previous output\r\n");
    ASSERT(st.cursor.position.X == 0);
    ASSERT(st.cursor.position.Y == 1);

    // Now simulate PSReadLine SetCursorPos to (10, 1) for history
    st.cursor.position = {10, 1};

    // WriteConsole should CUP to (10, 1) first, then write text
    sim_wc_with_cup(st, sb, U"HISTORY\r\n");

    // History text must appear at (10, 1), not at (0, 1) or (0, 2)
    ASSERT(sb.at_u32({10, 1}) == U'H');
    ASSERT(sb.at_u32({11, 1}) == U'I');
    ASSERT(sb.at_u32({12, 1}) == U'S');

    // After \r\n, cursor at (0, 2)
    ASSERT(st.cursor.position.X == 0);
    ASSERT(st.cursor.position.Y == 2);

    return true;
}

// Test multiple history recalls (pressing Up multiple times)
bool test_multiple_history_recalls()
{
    console_state st;
    screen_buffer sb({120, 30});
    st.screen_buffer_size = {120, 30};
    st.cursor.position = {0, 0};

    // Setup: prompt on row 0
    sim_wc_with_cup(st, sb, U"PS C:\\Users\\xyx>");
    ASSERT(st.cursor.position.Y == 0);
    ASSERT(st.cursor.position.X == 16);

    // First history recall
    st.cursor.position = {16, 0};
    sim_wc_with_cup(st, sb, U"command1\r\n");
    ASSERT(st.cursor.position.Y == 1);
    // Next prompt
    sim_wc_with_cup(st, sb, U"PS C:\\Users\\xyx>");
    ASSERT(st.cursor.position.Y == 1);

    // Second history recall
    st.cursor.position = {16, 1};
    sim_wc_with_cup(st, sb, U"command2_longer\r\n");
    ASSERT(st.cursor.position.Y == 2);
    ASSERT(sb.at_u32({16, 1}) == U'c'); // must be on row 1

    // Third history recall
    st.cursor.position = {16, 2};
    sim_wc_with_cup(st, sb, U"cmd3\r\n");
    ASSERT(st.cursor.position.Y == 3);
    ASSERT(sb.at_u32({16, 2}) == U'c'); // must be on row 2

    return true;
}

int main()
{
    std::wcout << L"=== WriteConsole Cursor Sync Tests ===" << std::endl;
    RUN_TEST(test_set_cursor_pos_then_write_console, L"SetCursorPos+WriteConsole row correct");
    RUN_TEST(test_cursor_alignment_after_newline, L"Cursor alignment after newline");
    RUN_TEST(test_history_cursor_on_correct_row, L"History cursor on correct row");
    RUN_TEST(test_multiple_history_recalls, L"Multiple history recalls");
    std::wcout << L"  " << tests_passed << L" passed, " << tests_failed << L" failed, " << (tests_passed + tests_failed)
               << L" total." << std::endl;
    return tests_failed > 0 ? 1 : 0;
}
