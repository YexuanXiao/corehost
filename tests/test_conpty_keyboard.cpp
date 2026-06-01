// === tests/test_conpty_keyboard.cpp ===
// Component-level keyboard input test.
// Tests VT parser -> vt_message -> INPUT_RECORD pipeline.
#include "test_common.hpp"
#include "conpty_vt_parser.hpp"
#include "vt_parser_test_helpers.hpp"
#include "conpty_vt_input_engine.hpp"
#include "console_state.hpp"
#include "screen_buffer.hpp"
#include "vt_msg_dispatch.hpp"

using namespace conpty;
using conpty::test::reset_test_vt_parser_message;

struct input_collector
{
    std::vector<INPUT_RECORD> records;
    void add(const INPUT_RECORD &ir)
    {
        records.push_back(ir);
    }
};

bool test_arrow_up()
{
    vt_input_engine engine;
    vt_message msg{};
    INPUT_RECORD rec{};
    ASSERT(engine.convert(vt_message_id::key_up, msg, rec));
    ASSERT(rec.Event.KeyEvent.wVirtualKeyCode == VK_UP);
    return true;
}

bool test_arrow_down()
{
    vt_input_engine engine;
    vt_message msg{};
    INPUT_RECORD rec{};
    ASSERT(engine.convert(vt_message_id::key_down, msg, rec));
    ASSERT(rec.Event.KeyEvent.wVirtualKeyCode == VK_DOWN);
    return true;
}

bool test_arrow_right_left()
{
    vt_input_engine engine;
    vt_message msg{};
    INPUT_RECORD rec{};
    ASSERT(engine.convert(vt_message_id::key_right, msg, rec));
    ASSERT(rec.Event.KeyEvent.wVirtualKeyCode == VK_RIGHT);
    ASSERT(engine.convert(vt_message_id::key_left, msg, rec));
    ASSERT(rec.Event.KeyEvent.wVirtualKeyCode == VK_LEFT);
    return true;
}

bool test_f3_key()
{
    vt_input_engine engine;
    vt_message msg{};
    INPUT_RECORD rec{};
    ASSERT(engine.convert(vt_message_id::key_f3, msg, rec));
    ASSERT(rec.Event.KeyEvent.wVirtualKeyCode == VK_F3);
    return true;
}

bool test_all_function_keys()
{
    vt_input_engine engine;
    vt_message msg{};
    const vt_message_id fkeys[] = {
        vt_message_id::key_f1, vt_message_id::key_f2,  vt_message_id::key_f3,  vt_message_id::key_f4,
        vt_message_id::key_f5, vt_message_id::key_f6,  vt_message_id::key_f7,  vt_message_id::key_f8,
        vt_message_id::key_f9, vt_message_id::key_f10, vt_message_id::key_f11, vt_message_id::key_f12,
    };
    const WORD vk[] = {VK_F1, VK_F2, VK_F3, VK_F4, VK_F5, VK_F6, VK_F7, VK_F8, VK_F9, VK_F10, VK_F11, VK_F12};
    for (int i = 0; i < 12; ++i)
    {
        INPUT_RECORD rec{};
        ASSERT(engine.convert(fkeys[i], msg, rec));
        ASSERT(rec.Event.KeyEvent.wVirtualKeyCode == vk[i]);
    }
    return true;
}

bool test_ctrl_arrow_keys()
{
    vt_input_engine engine;
    vt_message msg{};
    INPUT_RECORD rec{};
    ASSERT(engine.convert(vt_message_id::key_ctrl_up, msg, rec));
    ASSERT(rec.Event.KeyEvent.wVirtualKeyCode == VK_UP);
    ASSERT(rec.Event.KeyEvent.dwControlKeyState & LEFT_CTRL_PRESSED);
    ASSERT(engine.convert(vt_message_id::key_ctrl_down, msg, rec));
    ASSERT(rec.Event.KeyEvent.dwControlKeyState & LEFT_CTRL_PRESSED);
    return true;
}

bool test_navigation_keys()
{
    vt_input_engine engine;
    vt_message msg{};
    INPUT_RECORD rec{};
    ASSERT(engine.convert(vt_message_id::key_home, msg, rec));
    ASSERT(rec.Event.KeyEvent.wVirtualKeyCode == VK_HOME);
    ASSERT(engine.convert(vt_message_id::key_end, msg, rec));
    ASSERT(rec.Event.KeyEvent.wVirtualKeyCode == VK_END);
    ASSERT(engine.convert(vt_message_id::key_page_up, msg, rec));
    ASSERT(rec.Event.KeyEvent.wVirtualKeyCode == VK_PRIOR);
    ASSERT(engine.convert(vt_message_id::key_page_down, msg, rec));
    ASSERT(rec.Event.KeyEvent.wVirtualKeyCode == VK_NEXT);
    ASSERT(engine.convert(vt_message_id::key_insert, msg, rec));
    ASSERT(rec.Event.KeyEvent.wVirtualKeyCode == VK_INSERT);
    ASSERT(engine.convert(vt_message_id::key_delete, msg, rec));
    ASSERT(rec.Event.KeyEvent.wVirtualKeyCode == VK_DELETE);
    return true;
}

bool test_special_char_keys()
{
    vt_input_engine engine;
    vt_message msg{};
    INPUT_RECORD rec{};
    ASSERT(engine.convert(vt_message_id::char_del, msg, rec));
    ASSERT(rec.Event.KeyEvent.wVirtualKeyCode == VK_BACK);
    ASSERT(rec.Event.KeyEvent.uChar.UnicodeChar == L'\b');
    ASSERT(engine.convert(vt_message_id::char_esc, msg, rec));
    ASSERT(rec.Event.KeyEvent.wVirtualKeyCode == VK_ESCAPE);
    ASSERT(engine.convert(vt_message_id::char_sub, msg, rec));
    ASSERT(rec.Event.KeyEvent.wVirtualKeyCode == 26);
    return true;
}

bool test_unknown_id_returns_false()
{
    vt_input_engine engine;
    vt_message msg{};
    INPUT_RECORD rec{};
    ASSERT(!engine.convert(vt_message_id::sgr, msg, rec));
    return true;
}

bool test_full_pipeline_arrow_up()
{
    std::vector<char32_t> parser_raw;
    vt_parser parser{parser_raw};
    vt_input_engine engine;

    // ESC [ A = Arrow Up (CSI → cursor_up in parser)
    (void)parser.parse(0x1B);
    (void)parser.parse(U'[');
    auto id = parser.parse(U'A');

    ASSERT((id == vt_message_id::key_up || id == vt_message_id::cursor_up));

    // cursor_up must be mapped to key_up for engine.convert
    vt_message_id kid = id;
    if (kid == vt_message_id::cursor_up)
        kid = vt_message_id::key_up;
    INPUT_RECORD rec{};
    ASSERT(engine.convert(kid, parser.get(), rec));
    ASSERT(rec.Event.KeyEvent.wVirtualKeyCode == VK_UP);
    return true;
}

bool test_full_pipeline_arrow_down()
{
    std::vector<char32_t> parser_raw;
    vt_parser parser{parser_raw};
    vt_input_engine engine;

    // ESC [ B = Arrow Down (CSI → cursor_down in parser)
    (void)parser.parse(0x1B);
    (void)parser.parse(U'[');
    auto id = parser.parse(U'B');

    ASSERT((id == vt_message_id::key_down || id == vt_message_id::cursor_down));

    // cursor_down must be mapped to key_down for engine.convert
    vt_message_id kid = id;
    if (kid == vt_message_id::cursor_down)
        kid = vt_message_id::key_down;
    INPUT_RECORD rec{};
    ASSERT(engine.convert(kid, parser.get(), rec));
    ASSERT(rec.Event.KeyEvent.wVirtualKeyCode == VK_DOWN);
    return true;
}

// Regression: engine.convert does NOT handle cursor_up/cursor_down directly
// (pipe_bridge must map them to key_up/key_down first)
bool test_regression_cursor_updown_not_directly_convertible()
{
    vt_input_engine engine;
    vt_message msg{};
    INPUT_RECORD rec{};
    ASSERT(!engine.convert(vt_message_id::cursor_up, msg, rec));
    ASSERT(!engine.convert(vt_message_id::cursor_down, msg, rec));
    return true;
}

bool test_full_pipeline_f5()
{
    std::vector<char32_t> parser_raw;
    vt_parser parser{parser_raw};
    vt_input_engine engine;
    (void)parser.parse(0x1B);
    (void)parser.parse(U'[');
    (void)parser.parse(U'1');
    (void)parser.parse(U'5');
    auto id = parser.parse(U'~');
    ASSERT(id == vt_message_id::key_f5);
    INPUT_RECORD rec{};
    ASSERT(engine.convert(id, parser.get(), rec));
    ASSERT(rec.Event.KeyEvent.wVirtualKeyCode == VK_F5);
    return true;
}

bool test_text_to_screen_buffer()
{
    // Directly test vt_msg_apply_state with text message (bypass parser)
    console_state state;
    screen_buffer sb({80, 25});
    state.screen_buffer_size = {80, 25};
    state.cursor.position = {0, 0};

    vt_message msg{};
    msg.payload.text = U"Hello";
    vt_msg_apply_state(vt_message_id::text, msg, state, sb);

    ASSERT(state.cursor.position.X == 5);
    ASSERT(sb.at_u32({0, 0}) == U'H');
    ASSERT(sb.at_u32({4, 0}) == U'o');
    return true;
}

bool test_unicode_through_parser()
{
    std::vector<char32_t> parser_raw;
    vt_parser parser{parser_raw};

    // Feed U+1F600 (??)
    auto id = parser.parse(0x1F600);

    // The parser may return text or continue_ depending on internal state
    if (id == vt_message_id::text)
    {
        ASSERT(!parser.get().payload.text.empty());
    }
    else
    {
        ASSERT(id == vt_message_id::continue_text);
    }
    return true;
}

// ── Win32 Input Mode keyboard event tests ─────────────────
// Format: \x1b[Vk;Sc;Uc;Kd;Cs;Rc_
// Tests the full pipeline: raw bytes → VT parser → process_input → INPUT_RECORD

struct test_bridge_stub
{
    std::u32string _raw; // Parser 外部缓冲
    std::vector<char32_t> parser_raw;
    conpty::vt_parser parser{parser_raw};
    std::vector<INPUT_RECORD> events;

    test_bridge_stub() : parser(_raw)
    {
    }

    // Simulate process_input for a Win32Input byte sequence
    void feed_win32(const BYTE *bytes, DWORD len)
    {
        conpty::utf8_stream_decoder dec;
        for (DWORD i = 0; i < len; ++i)
        {
            BYTE b = bytes[i];
            auto ch = dec(static_cast<uint8_t>(b));
            if (!ch)
                continue;
            auto id = parser.parse(*ch);
            if (id == vt_message_id::continue_text || id == vt_message_id::continue_)
                continue;

            auto &m = parser.get();
            if (id == vt_message_id::win32_input_key)
            {
                INPUT_RECORD ir{};
                ir.EventType = KEY_EVENT;
                ir.Event.KeyEvent.bKeyDown = m.payload.win32_key.key_down ? TRUE : FALSE;
                ir.Event.KeyEvent.wRepeatCount = m.payload.win32_key.repeat_count;
                ir.Event.KeyEvent.wVirtualKeyCode = m.payload.win32_key.vk;
                ir.Event.KeyEvent.wVirtualScanCode = m.payload.win32_key.sc;
                ir.Event.KeyEvent.uChar.UnicodeChar = m.payload.win32_key.uc;
                ir.Event.KeyEvent.dwControlKeyState = m.payload.win32_key.control_state;
                events.push_back(ir);
            }
            reset_test_vt_parser_message(parser, id);
        }
    }

    void feed_text(const char *s)
    {
        feed_win32(reinterpret_cast<const BYTE *>(s), static_cast<DWORD>(std::strlen(s)));
    }
};

// Test Win32Input Enter key DOWN: \x1b[13;28;13;1;32;1_
bool test_win32_enter_down()
{
    test_bridge_stub stub;
    const BYTE seq[] = "\x1b[13;28;13;1;32;1_";
    stub.feed_win32(seq, sizeof(seq) - 1);
    ASSERT(stub.events.size() == 1);
    auto &e = stub.events[0];
    ASSERT(e.EventType == KEY_EVENT);
    ASSERT(e.Event.KeyEvent.bKeyDown == TRUE);
    ASSERT(e.Event.KeyEvent.wVirtualKeyCode == VK_RETURN);
    ASSERT(e.Event.KeyEvent.uChar.UnicodeChar == L'\r');
    return true;
}

// Test Win32Input Enter key UP: \x1b[13;28;13;0;32;1_
bool test_win32_enter_up()
{
    test_bridge_stub stub;
    const BYTE seq[] = "\x1b[13;28;13;0;32;1_";
    stub.feed_win32(seq, sizeof(seq) - 1);
    ASSERT(stub.events.size() == 1);
    auto &e = stub.events[0];
    ASSERT(e.EventType == KEY_EVENT);
    ASSERT(e.Event.KeyEvent.bKeyDown == FALSE);
    ASSERT(e.Event.KeyEvent.wVirtualKeyCode == VK_RETURN);
    return true;
}

// Test Win32Input Space DOWN: \x1b[32;57;32;1;32;1_
bool test_win32_space_down()
{
    test_bridge_stub stub;
    const BYTE seq[] = "\x1b[32;57;32;1;32;1_";
    stub.feed_win32(seq, sizeof(seq) - 1);
    ASSERT(stub.events.size() == 1);
    auto &e = stub.events[0];
    ASSERT(e.Event.KeyEvent.bKeyDown == TRUE);
    ASSERT(e.Event.KeyEvent.wVirtualKeyCode == VK_SPACE);
    ASSERT(e.Event.KeyEvent.uChar.UnicodeChar == L' ');
    return true;
}

// Test Win32Input Shift DOWN: \x1b[16;42;0;1;48;1_
bool test_win32_shift_down()
{
    test_bridge_stub stub;
    const BYTE seq[] = "\x1b[16;42;0;1;48;1_";
    stub.feed_win32(seq, sizeof(seq) - 1);
    ASSERT(stub.events.size() == 1);
    auto &e = stub.events[0];
    ASSERT(e.Event.KeyEvent.bKeyDown == TRUE);
    ASSERT(e.Event.KeyEvent.wVirtualKeyCode == VK_SHIFT);
    return true;
}

// Test Win32Input Tab DOWN: \x1b[9;15;9;1;0;1_
bool test_win32_tab_down()
{
    test_bridge_stub stub;
    const BYTE seq[] = "\x1b[9;15;9;1;0;1_";
    stub.feed_win32(seq, sizeof(seq) - 1);
    ASSERT(stub.events.size() == 1);
    auto &e = stub.events[0];
    ASSERT(e.Event.KeyEvent.bKeyDown == TRUE);
    ASSERT(e.Event.KeyEvent.wVirtualKeyCode == VK_TAB);
    ASSERT(e.Event.KeyEvent.uChar.UnicodeChar == L'\t');
    return true;
}

// Test Win32Input Escape DOWN: \x1b[27;1;27;1;0;1_
bool test_win32_escape_down()
{
    test_bridge_stub stub;
    const BYTE seq[] = "\x1b[27;1;27;1;0;1_";
    stub.feed_win32(seq, sizeof(seq) - 1);
    ASSERT(stub.events.size() == 1);
    auto &e = stub.events[0];
    ASSERT(e.Event.KeyEvent.bKeyDown == TRUE);
    ASSERT(e.Event.KeyEvent.wVirtualKeyCode == VK_ESCAPE);
    return true;
}

// Test Win32Input letter 'A' DOWN: \x1b[65;30;97;1;0;1_
bool test_win32_a_down()
{
    test_bridge_stub stub;
    const BYTE seq[] = "\x1b[65;30;97;1;0;1_";
    stub.feed_win32(seq, sizeof(seq) - 1);
    ASSERT(stub.events.size() == 1);
    auto &e = stub.events[0];
    ASSERT(e.Event.KeyEvent.bKeyDown == TRUE);
    ASSERT(e.Event.KeyEvent.wVirtualKeyCode == 0x41); // VK_A
    ASSERT(e.Event.KeyEvent.uChar.UnicodeChar == L'a');
    return true;
}

// Test Win32Input mixed: Enter DOWN + UP
bool test_win32_enter_pair()
{
    test_bridge_stub stub;
    const BYTE seq[] = "\x1b[13;28;13;1;32;1_\x1b[13;28;13;0;32;1_";
    stub.feed_win32(seq, sizeof(seq) - 1);
    ASSERT(stub.events.size() == 2);
    ASSERT(stub.events[0].Event.KeyEvent.bKeyDown == TRUE);
    ASSERT(stub.events[0].Event.KeyEvent.wVirtualKeyCode == VK_RETURN);
    ASSERT(stub.events[1].Event.KeyEvent.bKeyDown == FALSE);
    ASSERT(stub.events[1].Event.KeyEvent.wVirtualKeyCode == VK_RETURN);
    return true;
}

// Test plain text "A" (0x41) → emitted as continue_text (not Win32Input)
bool test_plain_text_a()
{
    test_bridge_stub stub;
    stub.feed_text("A");
    // Plain text in non-ConsoleRead path emits KEY_DOWN+KEY_UP via input_printable
    // But our stub doesn't call input_printable, so events=0. Just verify parsing.
    // This tests that the plain text parser doesn't crash.
    ASSERT(stub.events.size() == 0); // plain text goes through continue_text, not win32_input_key
    return true;
}

int main()
{
    std::wcout << L"=== ConPTY Keyboard Input Tests ===" << std::endl;
    RUN_TEST(test_arrow_up, L"Arrow Up");
    RUN_TEST(test_arrow_down, L"Arrow Down");
    RUN_TEST(test_arrow_right_left, L"Arrow Right/Left");
    RUN_TEST(test_f3_key, L"F3 key");
    RUN_TEST(test_all_function_keys, L"All F1-F12");
    RUN_TEST(test_ctrl_arrow_keys, L"Ctrl+Arrows");
    RUN_TEST(test_navigation_keys, L"Navigation keys");
    RUN_TEST(test_special_char_keys, L"Special chars");
    RUN_TEST(test_unknown_id_returns_false, L"Unknown->false");
    RUN_TEST(test_full_pipeline_arrow_up, L"Pipeline Arrow Up");
    RUN_TEST(test_full_pipeline_arrow_down, L"Pipeline Arrow Down");
    RUN_TEST(test_regression_cursor_updown_not_directly_convertible, L"cursor_up/down not convertible");
    RUN_TEST(test_full_pipeline_f5, L"Pipeline F5");
    RUN_TEST(test_text_to_screen_buffer, L"Text to SB");
    RUN_TEST(test_unicode_through_parser, L"Unicode");
    // Win32 Input Mode tests
    RUN_TEST(test_win32_enter_down, L"Win32EnterDown");
    RUN_TEST(test_win32_enter_up, L"Win32EnterUp");
    RUN_TEST(test_win32_space_down, L"Win32SpaceDown");
    RUN_TEST(test_win32_shift_down, L"Win32ShiftDown");
    RUN_TEST(test_win32_tab_down, L"Win32TabDown");
    RUN_TEST(test_win32_escape_down, L"Win32EscapeDown");
    RUN_TEST(test_win32_a_down, L"Win32ADown");
    RUN_TEST(test_win32_enter_pair, L"Win32EnterPair");
    RUN_TEST(test_plain_text_a, L"PlainTextA");

    std::wcout << L"  " << tests_passed << L" passed, " << tests_failed << L" failed, " << (tests_passed + tests_failed)
               << L" total." << std::endl;
    return tests_failed > 0 ? 1 : 0;
}
