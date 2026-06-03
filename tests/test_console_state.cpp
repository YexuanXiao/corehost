// ── tests/test_console_state.cpp ──────────────────────
// 控制台状态单元测试 (console_state.hpp)
//
// 覆盖: 光标、Tab 停靠位、模式、标题、别名、历史
#include "test_common.hpp"
#include "console_state.hpp"

using namespace corehost::conpty;

// ═══════════════════════════════════════════════════════
// 光标操作
// ═══════════════════════════════════════════════════════

bool test_cursor_default()
{
    console_state st;
    ASSERT(st.cursor.position.X == 0);
    ASSERT(st.cursor.position.Y == 0);
    ASSERT(st.cursor.visible == true);
    ASSERT(st.cursor.size == 25);
    return true;
}

bool test_cursor_visible()
{
    console_state st;
    st.cursor.visible = false;
    ASSERT(st.cursor.visible == false);
    return true;
}

bool test_cursor_save_restore()
{
    console_state st;
    st.cursor.position = {10, 5};
    st.cursor.visible = false;

    st.decsc_cursor.position = st.cursor.position;
    st.decsc_cursor.attributes = 0x1F;
    st.decsc_cursor.has_state = true;

    // 修改光标
    st.cursor.position = {0, 0};
    st.cursor.visible = true;

    // 恢复
    ASSERT(st.decsc_cursor.has_state);
    st.cursor.position = st.decsc_cursor.position;
    ASSERT(st.cursor.position.X == 10);
    ASSERT(st.cursor.position.Y == 5);
    return true;
}

// ═══════════════════════════════════════════════════════
// Tab 停靠位
// ═══════════════════════════════════════════════════════

bool test_tab_stops_init()
{
    console_state st;
    st.init_tab_stops();
    // 默认每 8 列设置 tab，从列 tab_width 开始
    ASSERT(st.tab_stops[8] == true);
    ASSERT(st.tab_stops[16] == true);
    ASSERT(st.tab_stops[24] == true);
    return true;
}

bool test_tab_set_and_clear()
{
    console_state st;
    st.init_tab_stops();

    // 在列 5 设置 tab
    st.set_tab_stop(5);
    ASSERT(st.tab_stops[5] == true);

    // 清除
    st.clear_tab_stop(5);
    ASSERT(st.tab_stops[5] == false);

    // 清除全部
    st.clear_all_tab_stops();
    ASSERT(st.tab_stops[8] == false);
    ASSERT(st.tab_stops[16] == false);
    return true;
}

bool test_tab_stop_next()
{
    console_state st;
    st.init_tab_stops();
    // 从列 0 开始找下一个 tab → 8
    auto nx = st.next_tab_stop(0);
    ASSERT(nx == 8);

    // 在列 10 找 → 16
    nx = st.next_tab_stop(10);
    ASSERT(nx == 16);

    // 超出最大值 → 返回默认最大列
    nx = st.next_tab_stop(500);
    ASSERT(nx >= 500);
    return true;
}

bool test_tab_stop_prev()
{
    console_state st;
    st.init_tab_stops();
    // 从列 10 找上一个 → 8
    auto pv = st.prev_tab_stop(10);
    ASSERT(pv == 8);

    // 从列 5 找上一个 → 0
    pv = st.prev_tab_stop(5);
    ASSERT(pv == 0);
    return true;
}

// ═══════════════════════════════════════════════════════
// 模式
// ═══════════════════════════════════════════════════════

bool test_default_mode()
{
    console_state st;
    ASSERT(st.input_mode == (ENABLE_PROCESSED_INPUT | ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_MOUSE_INPUT));
    ASSERT(st.output_mode == (ENABLE_PROCESSED_OUTPUT | ENABLE_WRAP_AT_EOL_OUTPUT));
    return true;
}

bool test_mode_bits()
{
    console_state st;
    st.input_mode |= ENABLE_ECHO_INPUT;
    ASSERT(st.input_mode & ENABLE_ECHO_INPUT);
    st.input_mode &= ~ENABLE_ECHO_INPUT;
    ASSERT(!(st.input_mode & ENABLE_ECHO_INPUT));
    return true;
}

bool test_codepage()
{
    console_state st;
    st.input_code_page = CP_UTF8;
    st.output_code_page = 936; // GBK
    ASSERT(st.input_code_page == CP_UTF8);
    ASSERT(st.output_code_page == 936);
    return true;
}

// ═══════════════════════════════════════════════════════
// 标题
// ═══════════════════════════════════════════════════════

bool test_title_initial()
{
    console_state st;
    ASSERT(st.title.empty());
    ASSERT(st.original_title.empty());
    return true;
}

bool test_title_set()
{
    console_state st;
    st.title = U"测试标题";
    ASSERT(st.title == U"测试标题");
    // original_title 不受影响
    ASSERT(st.original_title.empty());
    return true;
}

bool test_original_title()
{
    console_state st;
    // 模拟 SetTitle: 首次设置时保存 original_title
    st.original_title = U"原始标题";
    st.title = U"新标题";
    ASSERT(st.original_title == U"原始标题");
    ASSERT(st.title == U"新标题");
    return true;
}

bool test_history_settings_default()
{
    console_state st;
    ASSERT(st.history_buffer_size == 50);
    ASSERT(st.history_num_buffers == 4);
    ASSERT(st.history_flags == 0);
    return true;
}

// ═══════════════════════════════════════════════════════
// DOSKEY 别名
// ═══════════════════════════════════════════════════════

bool test_alias_add_and_find()
{
    console_state st;
    st.aliases[L"ls"] = L"dir";
    st.aliases[L"cl"] = L"cls";
    ASSERT(st.aliases.size() == 2);
    ASSERT(st.aliases[L"ls"] == L"dir");
    ASSERT(st.aliases[L"cl"] == L"cls");

    // 查找不存在的 key
    ASSERT(st.aliases.find(L"nobody") == st.aliases.end());
    return true;
}

bool test_alias_empty()
{
    console_state st;
    ASSERT(st.aliases.empty());
    ASSERT(st.aliases.find(L"x") == st.aliases.end());
    return true;
}

// ── AddAlias 消息格式回归测试（CONSOLE_ADDALIAS_MSG 布局）──
// 2026-05-21 修复: SourceLength/TargetLength/ExeLength 是字节数，
// 消息体 = Exe + Source + Target 三段连续布局。

#include "api_handlers.hpp"
#include "pipe_bridge_testable.hpp"
#include "message_router.hpp"
#include "os/Console/ntcon.h"
#include "os/Console/conmsgl3.h"
struct api_test_context
{
    screen_buffer sb;
    input_buffer inp;
    console_state state;
    pipe_bridge_testable bridge;

    api_test_context() : bridge(inp, state, sb)
    {
    }
};

api_test_context api_ctx;

void mock_get_console_input_msg(miniio::io_msg &msg, USHORT flags)
{
    std::memset(&msg, 0, sizeof(msg));
    msg.descriptor.OutputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETCONSOLEINPUT_MSG) + sizeof(INPUT_RECORD);

    auto *hdr = reinterpret_cast<CONSOLE_MSG_HEADER *>(msg.body);
    hdr->ApiNumber = static_cast<ULONG>(ConsolepGetConsoleInput);
    hdr->ApiDescriptorSize = sizeof(CONSOLE_GETCONSOLEINPUT_MSG);

    auto *input = reinterpret_cast<CONSOLE_GETCONSOLEINPUT_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    input->Flags = flags;
    input->Unicode = TRUE;
}

CONSOLE_GETCONSOLEINPUT_MSG *get_console_input_completion(miniio::io_msg &msg)
{
    return reinterpret_cast<CONSOLE_GETCONSOLEINPUT_MSG *>(msg.complete.Write.Data);
}

INPUT_RECORD *get_console_input_completion_records(miniio::io_msg &msg)
{
    return reinterpret_cast<INPUT_RECORD *>(static_cast<BYTE *>(msg.complete.Write.Data) +
                                            sizeof(CONSOLE_GETCONSOLEINPUT_MSG));
}

std::u32string read_key_down_chars(input_buffer &inp)
{
    std::u32string chars;
    INPUT_RECORD record{};
    while (inp.read(&record, 1) == 1)
    {
        if (record.EventType == KEY_EVENT && record.Event.KeyEvent.bKeyDown &&
            record.Event.KeyEvent.uChar.UnicodeChar != 0)
        {
            chars.push_back(static_cast<char32_t>(record.Event.KeyEvent.uChar.UnicodeChar));
        }
    }
    return chars;
}

// 辅助: 构造模拟 ConDrv AddAlias 消息 (Unicode)
void mock_add_alias_msg(miniio::io_msg &msg, const std::wstring &exe, const std::wstring &src, const std::wstring &tgt)
{
    std::memset(&msg, 0, sizeof(msg));
    auto *hdr = reinterpret_cast<CONSOLE_MSG_HEADER *>(msg.body);
    hdr->ApiNumber = static_cast<ULONG>(ConsolepAddAlias); // 0x12 L3-18
    hdr->ApiDescriptorSize = sizeof(CONSOLE_ADDALIAS_MSG);
    msg.descriptor.InputSize = static_cast<ULONG>(sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_ADDALIAS_MSG) +
                                                  (exe.size() + src.size() + tgt.size()) * sizeof(wchar_t));

    auto *alias = reinterpret_cast<CONSOLE_ADDALIAS_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    alias->SourceLength = static_cast<USHORT>(src.size() * sizeof(wchar_t));
    alias->TargetLength = static_cast<USHORT>(tgt.size() * sizeof(wchar_t));
    alias->ExeLength = static_cast<USHORT>(exe.size() * sizeof(wchar_t));
    alias->Unicode = TRUE;

    BYTE *data = msg.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_ADDALIAS_MSG);
    std::memcpy(data, exe.data(), exe.size() * sizeof(wchar_t));
    data += exe.size() * sizeof(wchar_t);
    std::memcpy(data, src.data(), src.size() * sizeof(wchar_t));
    data += src.size() * sizeof(wchar_t);
    std::memcpy(data, tgt.data(), tgt.size() * sizeof(wchar_t));
}

// 回归: AddAlias 消息正确解析 "hello" → "echo hello" (含 Exe="cmd.exe")
bool test_regression_add_alias_msg_layout()
{
    console_state st;
    miniio::io_msg msg;

    mock_add_alias_msg(msg, L"cmd.exe", L"hello", L"echo hello");
    api_l3_add_alias(msg, st, api_ctx.sb, api_ctx.inp, api_ctx.bridge);

    ASSERT(st.aliases.size() == 1);
    auto it = st.aliases.find(L"hello");
    ASSERT(it != st.aliases.end());
    ASSERT(it->second == L"echo hello");
    return true;
}

bool test_regression_get_console_input_nowait_empty()
{
    miniio::io_msg msg;
    console_state st;
    screen_buffer sb;
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};

    mock_get_console_input_msg(msg, CONSOLE_READ_NOWAIT);
    ASSERT(api_get_console_input(msg, st, sb, inp, bridge));

    auto *input = get_console_input_completion(msg);
    ASSERT(input->NumRecords == 0);
    return true;
}

bool test_regression_get_console_input_waits_when_empty()
{
    miniio::io_msg msg;
    console_state st;
    screen_buffer sb;
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};

    mock_get_console_input_msg(msg, 0);
    ASSERT(!api_get_console_input(msg, st, sb, inp, bridge));
    ASSERT(bridge.has_pending());
    return true;
}

bool test_regression_get_console_input_ready_event()
{
    miniio::io_msg msg;
    console_state st;
    screen_buffer sb;
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};

    INPUT_RECORD rec{};
    rec.EventType = KEY_EVENT;
    rec.Event.KeyEvent.bKeyDown = TRUE;
    rec.Event.KeyEvent.wVirtualKeyCode = L'A';
    rec.Event.KeyEvent.uChar.UnicodeChar = L'a';
    inp.write(&rec, 1);

    mock_get_console_input_msg(msg, 0);
    ASSERT(api_get_console_input(msg, st, sb, inp, bridge));

    auto *input = get_console_input_completion(msg);
    ASSERT(input->NumRecords == 1);
    return true;
}

bool test_regression_get_console_input_output_size_excludes_header()
{
    miniio::io_msg msg;
    console_state st;
    screen_buffer sb;
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};

    INPUT_RECORD rec{};
    rec.EventType = KEY_EVENT;
    rec.Event.KeyEvent.bKeyDown = TRUE;
    rec.Event.KeyEvent.wVirtualKeyCode = L'E';
    rec.Event.KeyEvent.uChar.UnicodeChar = L'e';
    inp.write(&rec, 1);

    mock_get_console_input_msg(msg, 0);
    msg.descriptor.OutputSize = sizeof(CONSOLE_GETCONSOLEINPUT_MSG) + sizeof(INPUT_RECORD);
    ASSERT(api_get_console_input(msg, st, sb, inp, bridge));

    auto *input = get_console_input_completion(msg);
    auto *out = get_console_input_completion_records(msg);
    ASSERT(input->NumRecords == 1);
    ASSERT(out->EventType == KEY_EVENT);
    ASSERT(out->Event.KeyEvent.uChar.UnicodeChar == L'e');
    return true;
}

bool test_regression_get_console_input_output_size_without_record()
{
    miniio::io_msg msg;
    console_state st;
    screen_buffer sb;
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};

    INPUT_RECORD rec{};
    rec.EventType = KEY_EVENT;
    rec.Event.KeyEvent.bKeyDown = TRUE;
    rec.Event.KeyEvent.wVirtualKeyCode = L'E';
    rec.Event.KeyEvent.uChar.UnicodeChar = L'e';
    inp.write(&rec, 1);

    mock_get_console_input_msg(msg, 0);
    msg.descriptor.OutputSize = sizeof(CONSOLE_GETCONSOLEINPUT_MSG);
    ASSERT(api_get_console_input(msg, st, sb, inp, bridge));

    auto *input = get_console_input_completion(msg);
    ASSERT(input->NumRecords == 0);
    ASSERT(inp.available() == 1);
    return true;
}

bool test_regression_get_console_input_uses_completion_buffer_for_large_output()
{
    miniio::io_msg msg;
    console_state st;
    screen_buffer sb;
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};

    constexpr size_t record_count = 256;
    INPUT_RECORD rec{};
    rec.EventType = KEY_EVENT;
    rec.Event.KeyEvent.bKeyDown = TRUE;
    rec.Event.KeyEvent.wVirtualKeyCode = L'A';
    for (size_t i = 0; i != record_count; ++i)
    {
        rec.Event.KeyEvent.uChar.UnicodeChar = static_cast<WCHAR>(L'A' + (i % 26));
        inp.write(&rec, 1);
    }

    mock_get_console_input_msg(msg, 0);
    msg.descriptor.OutputSize = sizeof(CONSOLE_GETCONSOLEINPUT_MSG) + record_count * sizeof(INPUT_RECORD);
    ASSERT(api_get_console_input(msg, st, sb, inp, bridge));

    auto *input = get_console_input_completion(msg);
    auto *out = get_console_input_completion_records(msg);
    ASSERT(input->NumRecords == record_count);
    ASSERT(msg.complete.Write.Size == sizeof(CONSOLE_GETCONSOLEINPUT_MSG) + record_count * sizeof(INPUT_RECORD));
    ASSERT(msg.complete.Write.Data != msg.body + sizeof(CONSOLE_MSG_HEADER));
    ASSERT(out[0].Event.KeyEvent.uChar.UnicodeChar == L'A');
    ASSERT(out[record_count - 1].Event.KeyEvent.uChar.UnicodeChar ==
           static_cast<WCHAR>(L'A' + ((record_count - 1) % 26)));
    ASSERT(inp.available() == 0);
    return true;
}

bool test_regression_signal_shutdown_requests_exit_only_without_pending()
{
    console_state st;
    screen_buffer sb;
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};

    win32::handle shutdown_event{::CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    ASSERT(shutdown_event.valid());
    bridge.set_signal_shutdown_event(shutdown_event.view());
    ASSERT(!bridge.should_exit());

    ASSERT(::SetEvent(shutdown_event.get()) != FALSE);
    ASSERT(bridge.should_exit());

    bridge.test_enter_console_read_mode();
    ASSERT(!bridge.should_exit());
    return true;
}

bool test_regression_get_console_input_rejects_invalid_flags()
{
    miniio::io_msg msg;
    console_state st;
    screen_buffer sb;
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};

    mock_get_console_input_msg(msg, CONSOLE_READ_VALID | 0x8000);
    ASSERT(api_get_console_input(msg, st, sb, inp, bridge));
    ASSERT(msg.complete.IoStatus.Status == status_invalid_parameter);
    ASSERT(!bridge.has_pending());
    return true;
}

bool test_regression_raw_write_decodes_output_codepage()
{
    miniio::io_msg msg{};
    console_state st;
    screen_buffer sb;
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};

    st.output_code_page = 936;
    const BYTE text[] = {0xCF, 0xB2, 0xBB, 0xB6, 0xC4, 0xE3};
    std::memcpy(msg.body, text, sizeof(text));
    msg.descriptor.InputSize = sizeof(text);

    ASSERT(api_raw_write_console(msg, st, sb, inp, bridge));
    ASSERT(msg.complete.IoStatus.Information == sizeof(text));
    ASSERT(sb.at_u32({0, 0}) == U'\u559C');
    ASSERT(sb.at_u32({2, 0}) == U'\u6B22');
    ASSERT(sb.at_u32({4, 0}) == U'\u4F60');
    ASSERT(st.cursor.position.X == 6);
    return true;
}

bool test_regression_raw_read_completion_writes_only_bytes()
{
    miniio::io_msg msg{};
    console_state st;
    screen_buffer sb;
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};

    const BYTE text[] = {'a', 'b', 'c', '\r'};
    msg.descriptor.OutputSize = 16;
    bridge.test_prepare_raw_read_completion(msg, text, sizeof(text));

    ASSERT(msg.complete.IoStatus.Information == 5);
    ASSERT(msg.complete.Write.Data == msg.body);
    ASSERT(msg.complete.Write.Size == 5);
    ASSERT(std::memcmp(msg.body, "abc\r\n", 5) == 0);
    return true;
}

bool test_regression_raw_read_completion_respects_output_size()
{
    miniio::io_msg msg{};
    console_state st;
    screen_buffer sb;
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};

    const BYTE text[] = {'a', 'b', 'c', '\r'};
    msg.descriptor.OutputSize = 4;
    bridge.test_prepare_raw_read_completion(msg, text, sizeof(text));

    ASSERT(msg.complete.IoStatus.Information == 4);
    ASSERT(msg.complete.Write.Size == 4);
    ASSERT(std::memcmp(msg.body, "abc\r", 4) == 0);
    return true;
}

bool test_regression_read_console_a_uses_input_codepage()
{
    miniio::io_msg msg{};
    console_state st;
    screen_buffer sb;
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};

    st.input_code_page = 936;
    msg.descriptor.OutputSize = sizeof(CONSOLE_READCONSOLE_MSG) + 16;
    auto *req = reinterpret_cast<CONSOLE_READCONSOLE_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    req->Unicode = FALSE;

    bridge.test_prepare_console_read_completion(msg, U"\u559C\u6B22\u4F60", false);

    const BYTE expected[] = {0xCF, 0xB2, 0xBB, 0xB6, 0xC4, 0xE3, '\r', '\n'};
    auto *out = msg.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLE_MSG);
    ASSERT(req->NumBytes == sizeof(expected));
    ASSERT(msg.complete.IoStatus.Information == sizeof(CONSOLE_READCONSOLE_MSG) + sizeof(expected));
    ASSERT(std::memcmp(out, expected, sizeof(expected)) == 0);
    return true;
}

bool test_regression_read_console_initial_bytes_check_output_capacity()
{
    miniio::io_msg msg{};
    console_state st;
    screen_buffer sb;
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};

    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLE_MSG);
    msg.descriptor.OutputSize = sizeof(CONSOLE_READCONSOLE_MSG) + 2;
    auto *req = reinterpret_cast<CONSOLE_READCONSOLE_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    req->Unicode = TRUE;
    req->InitialNumBytes = 4;

    ASSERT(api_read_console(msg, st, sb, inp, bridge));
    ASSERT(msg.complete.IoStatus.Status == status_invalid_parameter);
    ASSERT(!bridge.has_pending());
    return true;
}

bool test_regression_write_console_rejects_short_message()
{
    miniio::io_msg msg{};
    console_state st;
    screen_buffer sb;
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};

    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_WRITECONSOLE_MSG) - 1;
    ASSERT(api_write_console(msg, st, sb, inp, bridge));
    ASSERT(msg.complete.IoStatus.Status == status_invalid_parameter);
    return true;
}

bool test_regression_write_console_w_reports_complete_utf16_units()
{
    miniio::io_msg msg{};
    console_state st;
    screen_buffer sb;
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};

    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_WRITECONSOLE_MSG) + 3;
    auto *req = reinterpret_cast<CONSOLE_WRITECONSOLE_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    req->Unicode = TRUE;
    auto *payload = msg.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_WRITECONSOLE_MSG);
    const wchar_t ch = L'A';
    std::memcpy(payload, &ch, sizeof(ch));
    payload[sizeof(ch)] = 0x7F;

    ASSERT(api_write_console(msg, st, sb, inp, bridge));
    ASSERT(req->NumBytes == sizeof(wchar_t));
    ASSERT(sb.at_u32({0, 0}) == U'A');
    return true;
}

bool test_regression_write_console_answers_terminal_cpr_and_da_queries()
{
    console_state st;
    screen_buffer sb;
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};

    st.output_mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    st.cursor.position = {4, 6};
    bridge.test_set_term_cursor_valid(st.cursor.position);
    const std::wstring queries = L"\x1b[6n\x1b[c";
    write_console_payload(true, reinterpret_cast<const BYTE *>(queries.data()),
                          static_cast<ULONG>(queries.size() * sizeof(wchar_t)), st, sb, bridge, false);

    const auto response = read_key_down_chars(inp);
    ASSERT(response == U"\x1b[7;5R\x1b[?1;0c");
    ASSERT(bridge.test_vt_buf_len() == 0);
    return true;
}

bool test_regression_write_console_cpr_response_uses_viewport_relative_cursor()
{
    console_state st;
    screen_buffer sb{{120, 60}};
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};

    st.screen_buffer_size = {120, 60};
    st.max_window_size = {120, 30};
    sb.viewport.set_rect({10, 20, 109, 49}, st.screen_buffer_size);
    st.cursor.position = {15, 22};
    bridge.test_set_term_cursor_valid(sb.viewport.relative_position(st.cursor.position));

    const std::wstring query = L"\x1b[6n";
    write_console_payload(true, reinterpret_cast<const BYTE *>(query.data()),
                          static_cast<ULONG>(query.size() * sizeof(wchar_t)), st, sb, bridge, false);

    ASSERT(read_key_down_chars(inp) == U"\x1b[3;6R");
    return true;
}

bool test_regression_deprecated_l1_returns_not_implemented()
{
    miniio::io_msg msg{};
    console_state st;
    screen_buffer sb;
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};

    ASSERT(api_deprecated_l1(msg, st, sb, inp, bridge));
    ASSERT(msg.complete.IoStatus.Status == status_not_implemented);
    return true;
}

bool test_regression_get_langid_matches_original_gate()
{
    miniio::io_msg msg{};
    console_state st;
    screen_buffer sb;
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};

    st.output_code_page = code_page_chinese_simplified;
    ASSERT(api_get_langid(msg, st, sb, inp, bridge));

    if (is_east_asian_code_page(::GetACP()))
    {
        auto *lang = reinterpret_cast<CONSOLE_LANGID_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
        ASSERT(msg.complete.IoStatus.Status == 0);
        ASSERT(lang->LangId == MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED));
    }
    else
    {
        ASSERT(msg.complete.IoStatus.Status == status_not_supported);
    }

    ASSERT(lang_id_from_console_output_code_page(code_page_japanese) == MAKELANGID(LANG_JAPANESE, SUBLANG_DEFAULT));
    ASSERT(lang_id_from_console_output_code_page(code_page_korean) == MAKELANGID(LANG_KOREAN, SUBLANG_KOREAN));
    ASSERT(lang_id_from_console_output_code_page(code_page_chinese_traditional) ==
           MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_TRADITIONAL));
    return true;
}

bool test_regression_fill_console_output_a_uses_output_codepage()
{
    miniio::io_msg msg{};
    console_state st;
    screen_buffer sb;
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};

    st.output_code_page = 437;
    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_FILLCONSOLEOUTPUT_MSG);
    auto *fill = reinterpret_cast<CONSOLE_FILLCONSOLEOUTPUT_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    fill->ElementType = CONSOLE_ASCII;
    fill->Element = 0xB3;
    fill->Length = 1;
    fill->WriteCoord = {0, 0};

    ASSERT(api_fill_output(msg, st, sb, inp, bridge));
    ASSERT(fill->Length == 1);
    ASSERT(sb.at_u32({0, 0}) == U'\u2502');
    return true;
}

bool test_regression_fill_console_output_rejects_short_message()
{
    miniio::io_msg msg{};
    console_state st;
    screen_buffer sb;
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};

    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_FILLCONSOLEOUTPUT_MSG) - 1;
    ASSERT(api_fill_output(msg, st, sb, inp, bridge));
    ASSERT(msg.complete.IoStatus.Status == status_invalid_parameter);
    return true;
}

bool test_regression_fill_console_output_attr_preserves_current_attr()
{
    miniio::io_msg msg{};
    console_state st;
    screen_buffer sb;
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};

    st.default_attributes = 0x1E;
    sb.set_u32({0, 0}, U'X', 0x07);
    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_FILLCONSOLEOUTPUT_MSG);
    auto *fill = reinterpret_cast<CONSOLE_FILLCONSOLEOUTPUT_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    fill->ElementType = CONSOLE_ATTRIBUTE;
    fill->Element = 0x2C;
    fill->Length = 1;
    fill->WriteCoord = {0, 0};

    ASSERT(api_fill_output(msg, st, sb, inp, bridge));
    ASSERT(fill->Length == 1);
    ASSERT(sb.attr_at({0, 0}) == 0x2C);
    ASSERT(sb.at_u32({0, 0}) == U'X');
    ASSERT(st.default_attributes == 0x1E);
    return true;
}

bool test_regression_ctrl_event_rejects_short_message()
{
    miniio::io_msg msg{};
    console_state st;
    screen_buffer sb;
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};

    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_CTRLEVENT_MSG) - 1;
    ASSERT(api_ctrl_event(msg, st, sb, inp, bridge));
    ASSERT(msg.complete.IoStatus.Status == status_invalid_parameter);
    return true;
}

bool test_regression_set_console_cp_rejects_short_message()
{
    miniio::io_msg msg{};
    console_state st;
    screen_buffer sb;
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};

    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_SETCP_MSG) - 1;
    ASSERT(api_set_cp(msg, st, sb, inp, bridge));
    ASSERT(msg.complete.IoStatus.Status == status_invalid_parameter);
    return true;
}

bool test_regression_set_console_cp_updates_selected_codepage()
{
    miniio::io_msg msg{};
    console_state st;
    screen_buffer sb;
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};

    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_SETCP_MSG);
    auto *cp = reinterpret_cast<CONSOLE_SETCP_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    cp->Output = FALSE;
    cp->CodePage = 65001;
    ASSERT(api_set_cp(msg, st, sb, inp, bridge));
    ASSERT(msg.complete.IoStatus.Status == 0);
    ASSERT(st.input_code_page == 65001);

    std::memset(&msg, 0, sizeof(msg));
    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_SETCP_MSG);
    cp = reinterpret_cast<CONSOLE_SETCP_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    cp->Output = TRUE;
    cp->CodePage = 65001;
    ASSERT(api_set_cp(msg, st, sb, inp, bridge));
    ASSERT(st.output_code_page == 65001);
    return true;
}

bool test_regression_cursor_info_rejects_short_messages()
{
    miniio::io_msg msg{};
    console_state st;
    screen_buffer sb;
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};

    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETCURSORINFO_MSG) - 1;
    ASSERT(api_get_cursor(msg, st, sb, inp, bridge));
    ASSERT(msg.complete.IoStatus.Status == status_invalid_parameter);

    std::memset(&msg, 0, sizeof(msg));
    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_SETCURSORINFO_MSG) - 1;
    ASSERT(api_set_cursor(msg, st, sb, inp, bridge));
    ASSERT(msg.complete.IoStatus.Status == status_invalid_parameter);
    return true;
}

bool test_regression_get_screen_buffer_info_rejects_short_message()
{
    miniio::io_msg msg{};
    console_state st;
    screen_buffer sb;
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};

    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_SCREENBUFFERINFO_MSG) - 1;
    ASSERT(api_get_sb_info(msg, st, sb, inp, bridge));
    ASSERT(msg.complete.IoStatus.Status == status_invalid_parameter);
    return true;
}

bool test_regression_set_screen_buffer_info_validation()
{
    miniio::io_msg msg{};
    console_state st;
    screen_buffer sb;
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};

    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_SCREENBUFFERINFO_MSG) - 1;
    ASSERT(api_set_sb_info(msg, st, sb, inp, bridge));
    ASSERT(msg.complete.IoStatus.Status == status_invalid_parameter);

    std::memset(&msg, 0, sizeof(msg));
    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_SCREENBUFFERINFO_MSG);
    auto *info = reinterpret_cast<CONSOLE_SCREENBUFFERINFO_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    info->Size = {120, 30};
    info->CurrentWindowSize = {0, 0};
    ASSERT(api_set_sb_info(msg, st, sb, inp, bridge));
    ASSERT(msg.complete.IoStatus.Status == status_invalid_parameter);
    return true;
}

bool test_regression_set_screen_buffer_size_rejects_short_message()
{
    miniio::io_msg msg{};
    console_state st;
    screen_buffer sb;
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};

    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_SETSCREENBUFFERSIZE_MSG) - 1;
    ASSERT(api_set_sb_size(msg, st, sb, inp, bridge));
    ASSERT(msg.complete.IoStatus.Status == status_invalid_parameter);
    return true;
}

bool test_regression_set_cursor_position_rejects_short_message()
{
    miniio::io_msg msg{};
    console_state st;
    screen_buffer sb;
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};

    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_SETCURSORPOSITION_MSG) - 1;
    ASSERT(api_set_cursor_pos(msg, st, sb, inp, bridge));
    ASSERT(msg.complete.IoStatus.Status == status_invalid_parameter);
    return true;
}

bool test_regression_largest_window_rejects_short_message()
{
    miniio::io_msg msg{};
    console_state st;
    screen_buffer sb;
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};

    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETLARGESTWINDOWSIZE_MSG) - 1;
    ASSERT(api_largest_window(msg, st, sb, inp, bridge));
    ASSERT(msg.complete.IoStatus.Status == status_invalid_parameter);
    return true;
}

bool test_regression_scroll_screen_buffer_validation_and_ansi_fill()
{
    miniio::io_msg msg{};
    console_state st;
    screen_buffer sb;
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};

    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_SCROLLSCREENBUFFER_MSG) - 1;
    ASSERT(api_scroll_sb(msg, st, sb, inp, bridge));
    ASSERT(msg.complete.IoStatus.Status == status_invalid_parameter);

    std::memset(&msg, 0, sizeof(msg));
    st.output_code_page = 437;
    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_SCROLLSCREENBUFFER_MSG);
    auto *scroll = reinterpret_cast<CONSOLE_SCROLLSCREENBUFFER_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    scroll->Unicode = FALSE;
    scroll->ScrollRectangle = {0, 0, 0, 0};
    scroll->DestinationOrigin = {static_cast<SHORT>(sb.size.X), 0};
    scroll->Fill.Char.AsciiChar = static_cast<CHAR>(0xB3);
    scroll->Fill.Attributes = 0x0A;

    ASSERT(api_scroll_sb(msg, st, sb, inp, bridge));
    ASSERT(msg.complete.IoStatus.Status == 0);
    ASSERT(sb.at_u32({0, 0}) == U'\u2502');
    ASSERT(sb.attr_at({0, 0}) == 0x0A);
    return true;
}

bool test_regression_set_text_attribute_rejects_short_message()
{
    miniio::io_msg msg{};
    console_state st;
    screen_buffer sb;
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};

    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_SETTEXTATTRIBUTE_MSG) - 1;
    ASSERT(api_set_text_attr(msg, st, sb, inp, bridge));
    ASSERT(msg.complete.IoStatus.Status == status_invalid_parameter);
    return true;
}

bool test_regression_set_window_info_rejects_short_message()
{
    miniio::io_msg msg{};
    console_state st;
    screen_buffer sb;
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};

    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_SETWINDOWINFO_MSG) - 1;
    ASSERT(api_set_window_info(msg, st, sb, inp, bridge));
    ASSERT(msg.complete.IoStatus.Status == status_invalid_parameter);
    return true;
}

bool test_viewport_set_window_info_absolute_updates_origin_without_resizing_buffer()
{
    miniio::io_msg msg{};
    console_state st;
    screen_buffer sb;
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};

    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_SETWINDOWINFO_MSG);
    auto *window = reinterpret_cast<CONSOLE_SETWINDOWINFO_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    window->Absolute = TRUE;
    window->Window = {10, 5, 49, 14};

    ASSERT(api_set_window_info(msg, st, sb, inp, bridge));
    ASSERT(msg.complete.IoStatus.Status == 0);
    ASSERT(st.screen_buffer_size.X == default_console_size.X);
    ASSERT(st.screen_buffer_size.Y == default_console_size.Y);
    ASSERT(sb.viewport.origin().X == 10);
    ASSERT(sb.viewport.origin().Y == 5);
    ASSERT(sb.viewport.size().X == 40);
    ASSERT(sb.viewport.size().Y == 10);
    return true;
}

bool test_viewport_set_window_info_relative_offsets_current_rect()
{
    miniio::io_msg msg{};
    console_state st;
    screen_buffer sb;
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};

    sb.viewport.set_rect({10, 5, 49, 14}, sb.size);
    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_SETWINDOWINFO_MSG);
    auto *window = reinterpret_cast<CONSOLE_SETWINDOWINFO_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    window->Absolute = FALSE;
    window->Window = {1, 2, 1, 2};

    ASSERT(api_set_window_info(msg, st, sb, inp, bridge));
    ASSERT(msg.complete.IoStatus.Status == 0);
    const auto rect = sb.viewport.rect();
    ASSERT(rect.Left == 11);
    ASSERT(rect.Top == 7);
    ASSERT(rect.Right == 50);
    ASSERT(rect.Bottom == 16);
    return true;
}

bool test_viewport_set_cursor_position_snaps_cursor_into_view()
{
    miniio::io_msg msg{};
    console_state st;
    screen_buffer sb;
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};

    sb.viewport.set_rect({10, 5, 49, 14}, sb.size);
    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_SETCURSORPOSITION_MSG);
    auto *cursor = reinterpret_cast<CONSOLE_SETCURSORPOSITION_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    cursor->CursorPosition = {0, 0};

    ASSERT(api_set_cursor_pos(msg, st, sb, inp, bridge));
    ASSERT(msg.complete.IoStatus.Status == 0);
    ASSERT(st.cursor.position.X == 0);
    ASSERT(st.cursor.position.Y == 0);
    ASSERT(sb.viewport.origin().X == 0);
    ASSERT(sb.viewport.origin().Y == 0);
    ASSERT(sb.viewport.size().X == 40);
    ASSERT(sb.viewport.size().Y == 10);
    return true;
}

bool test_viewport_set_screen_buffer_info_resizes_view_without_moving_origin()
{
    miniio::io_msg msg{};
    console_state st;
    screen_buffer sb;
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};

    sb.viewport.set_rect({10, 5, 49, 14}, sb.size);
    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_SCREENBUFFERINFO_MSG);
    auto *info = reinterpret_cast<CONSOLE_SCREENBUFFERINFO_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    info->Size = {120, 50};
    info->ScrollPosition = {99, 99};
    info->CurrentWindowSize = {30, 8};
    info->MaximumWindowSize = {120, 50};
    info->Attributes = 0x07;
    info->PopupAttributes = 0x07;

    ASSERT(api_set_sb_info(msg, st, sb, inp, bridge));
    ASSERT(msg.complete.IoStatus.Status == 0);
    ASSERT(st.screen_buffer_size.X == 120);
    ASSERT(st.screen_buffer_size.Y == 50);
    ASSERT(sb.viewport.origin().X == 10);
    ASSERT(sb.viewport.origin().Y == 5);
    ASSERT(sb.viewport.size().X == 30);
    ASSERT(sb.viewport.size().Y == 8);
    return true;
}

bool test_viewport_state_is_owned_by_each_screen_buffer()
{
    console_state st;
    screen_buffer main_sb{{120, 30}};
    screen_buffer alt_sb{{120, 30}};
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, main_sb};

    main_sb.viewport.set_rect({10, 5, 49, 14}, main_sb.size);
    ASSERT(main_sb.viewport.origin().X == 10);
    ASSERT(main_sb.viewport.origin().Y == 5);
    ASSERT(alt_sb.viewport.origin().X == 0);
    ASSERT(alt_sb.viewport.origin().Y == 0);

    ASSERT(&bridge.active_screen_buffer() == &main_sb);
    bridge.set_active_screen_buffer(alt_sb);
    ASSERT(&bridge.active_screen_buffer() == &alt_sb);
    return true;
}

bool test_viewport_vt_cursor_position_updates_buffer_coordinates()
{
    console_state st;
    screen_buffer sb;
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};

    st.output_mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    sb.viewport.set_rect({10, 5, 49, 14}, sb.size);
    st.cursor.position = {10, 5};

    const std::wstring text = L"\x1b[2;3HX";
    write_console_payload(true, reinterpret_cast<const BYTE *>(text.data()),
                          static_cast<ULONG>(text.size() * sizeof(wchar_t)), st, sb, bridge);

    ASSERT(sb.at_u32({12, 6}) == U'X');
    ASSERT(st.cursor.position.X == 13);
    ASSERT(st.cursor.position.Y == 6);

    const std::wstring relative = L"\x1b[999DX";
    write_console_payload(true, reinterpret_cast<const BYTE *>(relative.data()),
                          static_cast<ULONG>(relative.size() * sizeof(wchar_t)), st, sb, bridge);

    ASSERT(sb.at_u32({10, 6}) == U'X');
    ASSERT(st.cursor.position.X == 11);
    ASSERT(st.cursor.position.Y == 6);

    sb.set_u32({0, 0}, U'Z');
    sb.set_u32({10, 5}, U'Y');
    const std::wstring erase_display = L"\x1b[2J";
    write_console_payload(true, reinterpret_cast<const BYTE *>(erase_display.data()),
                          static_cast<ULONG>(erase_display.size() * sizeof(wchar_t)), st, sb, bridge);

    ASSERT(sb.at_u32({10, 5}) == U' ');
    ASSERT(sb.at_u32({0, 0}) == U'Z');

    sb.set_u32({9, 6}, U'B');
    sb.set_u32({10, 6}, U'A');
    const std::wstring erase_line = L"\x1b[2K";
    write_console_payload(true, reinterpret_cast<const BYTE *>(erase_line.data()),
                          static_cast<ULONG>(erase_line.size() * sizeof(wchar_t)), st, sb, bridge);

    ASSERT(sb.at_u32({10, 6}) == U' ');
    ASSERT(sb.at_u32({9, 6}) == U'B');
    return true;
}

bool test_viewport_vt_scroll_is_clipped_to_visible_window()
{
    console_state st;
    screen_buffer sb;
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};

    st.output_mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    sb.viewport.set_rect({10, 5, 49, 14}, sb.size);
    st.cursor.position = {10, 5};

    sb.set_u32({10, 5}, U'A');
    sb.set_u32({10, 6}, U'B');
    sb.set_u32({10, 14}, U'Z');
    sb.set_u32({9, 5}, U'L');
    sb.set_u32({50, 5}, U'R');

    const std::wstring scroll_up = L"\x1b[S";
    write_console_payload(true, reinterpret_cast<const BYTE *>(scroll_up.data()),
                          static_cast<ULONG>(scroll_up.size() * sizeof(wchar_t)), st, sb, bridge);

    ASSERT(sb.at_u32({10, 5}) == U'B');
    ASSERT(sb.at_u32({10, 13}) == U'Z');
    ASSERT(sb.at_u32({10, 14}) == U' ');
    ASSERT(sb.at_u32({9, 5}) == U'L');
    ASSERT(sb.at_u32({50, 5}) == U'R');

    sb.set_u32({10, 5}, U'A');
    sb.set_u32({10, 6}, U'B');
    const std::wstring scroll_down = L"\x1b[T";
    write_console_payload(true, reinterpret_cast<const BYTE *>(scroll_down.data()),
                          static_cast<ULONG>(scroll_down.size() * sizeof(wchar_t)), st, sb, bridge);

    ASSERT(sb.at_u32({10, 5}) == U' ');
    ASSERT(sb.at_u32({10, 6}) == U'A');
    ASSERT(sb.at_u32({10, 7}) == U'B');
    ASSERT(sb.at_u32({9, 5}) == U'L');
    ASSERT(sb.at_u32({50, 5}) == U'R');
    return true;
}

bool test_viewport_vt_insert_delete_lines_start_at_cursor_row()
{
    console_state st;
    screen_buffer sb;
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};

    st.output_mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    sb.viewport.set_rect({10, 5, 49, 14}, sb.size);
    st.cursor.position = {12, 7};

    sb.set_u32({10, 5}, U'T');
    sb.set_u32({10, 7}, U'A');
    sb.set_u32({10, 8}, U'B');
    sb.set_u32({10, 14}, U'Z');
    sb.set_u32({9, 7}, U'L');

    const std::wstring insert_line = L"\x1b[L";
    write_console_payload(true, reinterpret_cast<const BYTE *>(insert_line.data()),
                          static_cast<ULONG>(insert_line.size() * sizeof(wchar_t)), st, sb, bridge);

    ASSERT(sb.at_u32({10, 5}) == U'T');
    ASSERT(sb.at_u32({10, 7}) == U' ');
    ASSERT(sb.at_u32({10, 8}) == U'A');
    ASSERT(sb.at_u32({10, 9}) == U'B');
    ASSERT(sb.at_u32({9, 7}) == U'L');

    sb.set_u32({10, 7}, U'A');
    sb.set_u32({10, 8}, U'B');
    const std::wstring delete_line = L"\x1b[M";
    write_console_payload(true, reinterpret_cast<const BYTE *>(delete_line.data()),
                          static_cast<ULONG>(delete_line.size() * sizeof(wchar_t)), st, sb, bridge);

    ASSERT(sb.at_u32({10, 5}) == U'T');
    ASSERT(sb.at_u32({10, 7}) == U'B');
    ASSERT(sb.at_u32({10, 14}) == U' ');
    ASSERT(sb.at_u32({9, 7}) == U'L');
    return true;
}

bool test_viewport_vt_text_wraps_inside_visible_window()
{
    console_state st;
    screen_buffer sb;
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};

    st.output_mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    sb.viewport.set_rect({10, 5, 12, 6}, sb.size);
    st.cursor.position = {10, 5};

    const std::wstring text = L"ABCD";
    write_console_payload(true, reinterpret_cast<const BYTE *>(text.data()),
                          static_cast<ULONG>(text.size() * sizeof(wchar_t)), st, sb, bridge);

    ASSERT(sb.at_u32({10, 5}) == U'A');
    ASSERT(sb.at_u32({11, 5}) == U'B');
    ASSERT(sb.at_u32({12, 5}) == U'C');
    ASSERT(sb.at_u32({10, 6}) == U'D');
    ASSERT(st.cursor.position.X == 11);
    ASSERT(st.cursor.position.Y == 6);
    return true;
}

bool test_viewport_vt_line_feed_scrolls_visible_window()
{
    console_state st;
    screen_buffer sb;
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};

    st.output_mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    sb.viewport.set_rect({10, 5, 12, 6}, sb.size);
    st.cursor.position = {10, 6};
    sb.set_u32({10, 5}, U'A');
    sb.set_u32({10, 6}, U'B');
    sb.set_u32({9, 6}, U'L');

    const std::wstring line_feed = L"\n";
    write_console_payload(true, reinterpret_cast<const BYTE *>(line_feed.data()),
                          static_cast<ULONG>(line_feed.size() * sizeof(wchar_t)), st, sb, bridge);

    ASSERT(sb.at_u32({10, 5}) == U'B');
    ASSERT(sb.at_u32({10, 6}) == U' ');
    ASSERT(sb.at_u32({9, 6}) == U'L');
    ASSERT(st.cursor.position.X == 10);
    ASSERT(st.cursor.position.Y == 6);
    return true;
}

bool test_viewport_vt_character_editing_is_clipped_to_visible_window()
{
    console_state st;
    screen_buffer sb;
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};

    st.output_mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    sb.viewport.set_rect({10, 5, 14, 5}, sb.size);
    st.cursor.position = {12, 5};
    sb.set_u32({9, 5}, U'L');
    sb.set_u32({15, 5}, U'R');

    auto set_visible_text = [&] {
        sb.set_u32({10, 5}, U'A');
        sb.set_u32({11, 5}, U'B');
        sb.set_u32({12, 5}, U'C');
        sb.set_u32({13, 5}, U'D');
        sb.set_u32({14, 5}, U'E');
    };

    set_visible_text();
    const std::wstring insert_char = L"\x1b[@";
    write_console_payload(true, reinterpret_cast<const BYTE *>(insert_char.data()),
                          static_cast<ULONG>(insert_char.size() * sizeof(wchar_t)), st, sb, bridge);
    ASSERT(sb.at_u32({10, 5}) == U'A');
    ASSERT(sb.at_u32({11, 5}) == U'B');
    ASSERT(sb.at_u32({12, 5}) == U' ');
    ASSERT(sb.at_u32({13, 5}) == U'C');
    ASSERT(sb.at_u32({14, 5}) == U'D');

    set_visible_text();
    const std::wstring delete_char = L"\x1b[P";
    write_console_payload(true, reinterpret_cast<const BYTE *>(delete_char.data()),
                          static_cast<ULONG>(delete_char.size() * sizeof(wchar_t)), st, sb, bridge);
    ASSERT(sb.at_u32({10, 5}) == U'A');
    ASSERT(sb.at_u32({11, 5}) == U'B');
    ASSERT(sb.at_u32({12, 5}) == U'D');
    ASSERT(sb.at_u32({13, 5}) == U'E');
    ASSERT(sb.at_u32({14, 5}) == U' ');

    set_visible_text();
    const std::wstring erase_char = L"\x1b[X";
    write_console_payload(true, reinterpret_cast<const BYTE *>(erase_char.data()),
                          static_cast<ULONG>(erase_char.size() * sizeof(wchar_t)), st, sb, bridge);
    ASSERT(sb.at_u32({10, 5}) == U'A');
    ASSERT(sb.at_u32({11, 5}) == U'B');
    ASSERT(sb.at_u32({12, 5}) == U' ');
    ASSERT(sb.at_u32({13, 5}) == U'D');
    ASSERT(sb.at_u32({14, 5}) == U'E');
    ASSERT(sb.at_u32({9, 5}) == U'L');
    ASSERT(sb.at_u32({15, 5}) == U'R');
    return true;
}

bool test_viewport_vt_reverse_index_scrolls_visible_window()
{
    console_state st;
    screen_buffer sb;
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};

    st.output_mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    sb.viewport.set_rect({10, 5, 12, 6}, sb.size);
    st.cursor.position = {11, 5};
    sb.set_u32({10, 5}, U'A');
    sb.set_u32({10, 6}, U'B');
    sb.set_u32({9, 5}, U'L');

    const std::wstring reverse_index = L"\x1bM";
    write_console_payload(true, reinterpret_cast<const BYTE *>(reverse_index.data()),
                          static_cast<ULONG>(reverse_index.size() * sizeof(wchar_t)), st, sb, bridge);

    ASSERT(sb.at_u32({10, 5}) == U' ');
    ASSERT(sb.at_u32({10, 6}) == U'A');
    ASSERT(sb.at_u32({9, 5}) == U'L');
    ASSERT(st.cursor.position.X == 11);
    ASSERT(st.cursor.position.Y == 5);
    return true;
}

bool test_viewport_vt_tabs_are_viewport_relative()
{
    console_state st;
    screen_buffer sb;
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};

    st.output_mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    sb.viewport.set_rect({10, 5, 49, 14}, sb.size);
    st.cursor.position = {11, 5};

    const std::wstring tab = L"\t";
    write_console_payload(true, reinterpret_cast<const BYTE *>(tab.data()),
                          static_cast<ULONG>(tab.size() * sizeof(wchar_t)), st, sb, bridge);
    ASSERT(st.cursor.position.X == 18);

    const std::wstring backward_tab = L"\x1b[Z";
    write_console_payload(true, reinterpret_cast<const BYTE *>(backward_tab.data()),
                          static_cast<ULONG>(backward_tab.size() * sizeof(wchar_t)), st, sb, bridge);
    ASSERT(st.cursor.position.X == 10);
    return true;
}

bool test_viewport_vt_scrolling_region_is_viewport_relative()
{
    console_state st;
    screen_buffer sb;
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};

    st.output_mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    sb.viewport.set_rect({10, 5, 12, 8}, sb.size);
    st.cursor.position = {10, 5};
    sb.set_u32({10, 5}, U'A');
    sb.set_u32({10, 6}, U'B');
    sb.set_u32({10, 7}, U'C');
    sb.set_u32({10, 8}, U'D');

    const std::wstring set_region = L"\x1b[2;3r";
    write_console_payload(true, reinterpret_cast<const BYTE *>(set_region.data()),
                          static_cast<ULONG>(set_region.size() * sizeof(wchar_t)), st, sb, bridge);

    ASSERT(st.scroll_region_top == 2);
    ASSERT(st.scroll_region_bottom == 3);
    ASSERT(st.cursor.position.X == 10);
    ASSERT(st.cursor.position.Y == 5);

    st.cursor.position = {10, 7};
    const std::wstring line_feed = L"\n";
    write_console_payload(true, reinterpret_cast<const BYTE *>(line_feed.data()),
                          static_cast<ULONG>(line_feed.size() * sizeof(wchar_t)), st, sb, bridge);

    ASSERT(sb.at_u32({10, 5}) == U'A');
    ASSERT(sb.at_u32({10, 6}) == U'C');
    ASSERT(sb.at_u32({10, 7}) == U' ');
    ASSERT(sb.at_u32({10, 8}) == U'D');

    sb.set_u32({10, 6}, U'B');
    sb.set_u32({10, 7}, U'C');
    st.cursor.position = {11, 6};
    const std::wstring reverse_index = L"\x1bM";
    write_console_payload(true, reinterpret_cast<const BYTE *>(reverse_index.data()),
                          static_cast<ULONG>(reverse_index.size() * sizeof(wchar_t)), st, sb, bridge);

    ASSERT(sb.at_u32({10, 5}) == U'A');
    ASSERT(sb.at_u32({10, 6}) == U' ');
    ASSERT(sb.at_u32({10, 7}) == U'B');
    ASSERT(sb.at_u32({10, 8}) == U'D');
    return true;
}

bool test_regression_read_output_string_output_size_and_linear_read()
{
    miniio::io_msg msg{};
    console_state st;
    screen_buffer sb{{2, 2}};
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};

    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLEOUTPUTSTRING_MSG) - 1;
    ASSERT(api_read_output_string(msg, st, sb, inp, bridge));
    ASSERT(msg.complete.IoStatus.Status == status_invalid_parameter);

    sb.set_u32({0, 0}, U'A');
    sb.set_u32({1, 0}, U'B');
    sb.set_u32({0, 1}, U'C');
    sb.set_u32({1, 1}, U'D');

    std::memset(&msg, 0, sizeof(msg));
    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLEOUTPUTSTRING_MSG);
    msg.descriptor.OutputSize = sizeof(CONSOLE_READCONSOLEOUTPUTSTRING_MSG) + 3 * sizeof(wchar_t);
    auto *read = reinterpret_cast<CONSOLE_READCONSOLEOUTPUTSTRING_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    read->StringType = CONSOLE_REAL_UNICODE;
    read->ReadCoord = {1, 0};

    ASSERT(api_read_output_string(msg, st, sb, inp, bridge));
    auto *out = reinterpret_cast<wchar_t *>(msg.body + sizeof(CONSOLE_MSG_HEADER) +
                                            sizeof(CONSOLE_READCONSOLEOUTPUTSTRING_MSG));
    ASSERT(read->NumRecords == 3);
    ASSERT(out[0] == L'B');
    ASSERT(out[1] == L'C');
    ASSERT(out[2] == L'D');
    return true;
}

bool test_regression_write_console_input_a_uses_input_codepage()
{
    miniio::io_msg msg{};
    console_state st;
    screen_buffer sb;
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};

    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_WRITECONSOLEINPUT_MSG) - 1;
    ASSERT(api_write_console_input(msg, st, sb, inp, bridge));
    ASSERT(msg.complete.IoStatus.Status == status_invalid_parameter);

    std::memset(&msg, 0, sizeof(msg));
    st.input_code_page = 936;
    msg.descriptor.InputSize =
        sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_WRITECONSOLEINPUT_MSG) + 2 * sizeof(INPUT_RECORD);
    auto *write = reinterpret_cast<CONSOLE_WRITECONSOLEINPUT_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    write->Unicode = FALSE;
    write->Append = TRUE;
    auto *records =
        reinterpret_cast<INPUT_RECORD *>(msg.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_WRITECONSOLEINPUT_MSG));
    records[0].EventType = KEY_EVENT;
    records[0].Event.KeyEvent.bKeyDown = TRUE;
    records[0].Event.KeyEvent.uChar.AsciiChar = static_cast<CHAR>(0xCF);
    records[1].EventType = KEY_EVENT;
    records[1].Event.KeyEvent.bKeyDown = TRUE;
    records[1].Event.KeyEvent.uChar.AsciiChar = static_cast<CHAR>(0xB2);

    ASSERT(api_write_console_input(msg, st, sb, inp, bridge));
    ASSERT(write->NumRecords == 1);
    INPUT_RECORD out{};
    ASSERT(inp.read(&out, 1) == 1);
    ASSERT(out.EventType == KEY_EVENT);
    ASSERT(out.Event.KeyEvent.uChar.UnicodeChar == L'\u559c');
    return true;
}

bool test_regression_write_console_output_validation_and_clipping()
{
    miniio::io_msg msg{};
    console_state st;
    screen_buffer sb{{2, 1}};
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};

    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_WRITECONSOLEOUTPUT_MSG) - 1;
    ASSERT(api_write_console_output(msg, st, sb, inp, bridge));
    ASSERT(msg.complete.IoStatus.Status == status_invalid_parameter);

    std::memset(&msg, 0, sizeof(msg));
    msg.descriptor.InputSize =
        sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_WRITECONSOLEOUTPUT_MSG) + 2 * sizeof(CHAR_INFO);
    auto *write = reinterpret_cast<CONSOLE_WRITECONSOLEOUTPUT_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    write->Unicode = TRUE;
    write->CharRegion = {-1, 0, 0, 0};
    auto *cells =
        reinterpret_cast<CHAR_INFO *>(msg.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_WRITECONSOLEOUTPUT_MSG));
    cells[0].Char.UnicodeChar = L'A';
    cells[0].Attributes = 0x07;
    cells[1].Char.UnicodeChar = L'B';
    cells[1].Attributes = 0x0A;

    ASSERT(api_write_console_output(msg, st, sb, inp, bridge));
    ASSERT(msg.complete.IoStatus.Status == 0);
    ASSERT(write->CharRegion.Left == 0);
    ASSERT(write->CharRegion.Right == 0);
    ASSERT(sb.at_u32({0, 0}) == U'B');
    ASSERT(sb.attr_at({0, 0}) == 0x0A);
    return true;
}

bool test_regression_write_output_string_linear_and_ansi_count()
{
    miniio::io_msg msg{};
    console_state st;
    screen_buffer sb{{2, 2}};
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};

    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_WRITECONSOLEOUTPUTSTRING_MSG) - 1;
    ASSERT(api_write_output_string(msg, st, sb, inp, bridge));
    ASSERT(msg.complete.IoStatus.Status == status_invalid_parameter);

    std::memset(&msg, 0, sizeof(msg));
    msg.descriptor.InputSize =
        sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_WRITECONSOLEOUTPUTSTRING_MSG) + 3 * sizeof(wchar_t);
    auto *write = reinterpret_cast<CONSOLE_WRITECONSOLEOUTPUTSTRING_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    write->StringType = CONSOLE_REAL_UNICODE;
    write->WriteCoord = {1, 0};
    auto *text = reinterpret_cast<wchar_t *>(msg.body + sizeof(CONSOLE_MSG_HEADER) +
                                             sizeof(CONSOLE_WRITECONSOLEOUTPUTSTRING_MSG));
    text[0] = L'X';
    text[1] = L'Y';
    text[2] = L'Z';

    ASSERT(api_write_output_string(msg, st, sb, inp, bridge));
    ASSERT(write->NumRecords == 3);
    ASSERT(sb.at_u32({1, 0}) == U'X');
    ASSERT(sb.at_u32({0, 1}) == U'Y');
    ASSERT(sb.at_u32({1, 1}) == U'Z');

    std::memset(&msg, 0, sizeof(msg));
    st.output_code_page = 936;
    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_WRITECONSOLEOUTPUTSTRING_MSG) + 2;
    write = reinterpret_cast<CONSOLE_WRITECONSOLEOUTPUTSTRING_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    write->StringType = CONSOLE_ASCII;
    write->WriteCoord = {0, 0};
    auto *bytes =
        reinterpret_cast<char *>(msg.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_WRITECONSOLEOUTPUTSTRING_MSG));
    bytes[0] = static_cast<char>(0xCF);
    bytes[1] = static_cast<char>(0xB2);

    ASSERT(api_write_output_string(msg, st, sb, inp, bridge));
    ASSERT(write->NumRecords == 2);
    ASSERT(sb.at_u32({0, 0}) == U'\u559c');
    return true;
}

bool test_regression_read_console_output_output_size_and_clipping()
{
    miniio::io_msg msg{};
    console_state st;
    screen_buffer sb{{2, 1}};
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};

    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLEOUTPUT_MSG) - 1;
    ASSERT(api_read_console_output(msg, st, sb, inp, bridge));
    ASSERT(msg.complete.IoStatus.Status == status_invalid_parameter);

    sb.set_u32({0, 0}, U'B', 0x0A);
    std::memset(&msg, 0, sizeof(msg));
    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLEOUTPUT_MSG);
    msg.descriptor.OutputSize = sizeof(CONSOLE_READCONSOLEOUTPUT_MSG) + 2 * sizeof(CHAR_INFO);
    auto *read = reinterpret_cast<CONSOLE_READCONSOLEOUTPUT_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    read->Unicode = TRUE;
    read->CharRegion = {-1, 0, 0, 0};

    ASSERT(api_read_console_output(msg, st, sb, inp, bridge));
    ASSERT(read->CharRegion.Left == 0);
    ASSERT(read->CharRegion.Right == 0);
    auto *cells =
        reinterpret_cast<CHAR_INFO *>(msg.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLEOUTPUT_MSG));
    ASSERT(cells[1].Char.UnicodeChar == L'B');
    ASSERT(cells[1].Attributes == 0x0A);
    return true;
}

bool test_regression_get_title_output_size_limits_copy()
{
    miniio::io_msg msg{};
    console_state st;
    screen_buffer sb;
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};

    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETTITLE_MSG) - 1;
    ASSERT(api_get_title(msg, st, sb, inp, bridge));
    ASSERT(msg.complete.IoStatus.Status == status_invalid_parameter);

    std::u32string title;
    convert_utf16_to_u32(std::wstring_view{L"test"}, title);
    st.title = title;

    std::memset(&msg, 0, sizeof(msg));
    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETTITLE_MSG);
    msg.descriptor.OutputSize = sizeof(CONSOLE_GETTITLE_MSG) + 2 * sizeof(wchar_t);
    auto *get = reinterpret_cast<CONSOLE_GETTITLE_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    get->Unicode = TRUE;
    auto *out = reinterpret_cast<wchar_t *>(msg.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETTITLE_MSG));

    ASSERT(api_get_title(msg, st, sb, inp, bridge));
    ASSERT(get->TitleLength == 8);
    ASSERT(out[0] == L't');
    ASSERT(out[1] == L'e');
    return true;
}

bool test_regression_set_title_a_uses_input_codepage()
{
    miniio::io_msg msg{};
    console_state st;
    screen_buffer sb;
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};

    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_SETTITLE_MSG) - 1;
    ASSERT(api_set_title(msg, st, sb, inp, bridge));
    ASSERT(msg.complete.IoStatus.Status == status_invalid_parameter);

    std::memset(&msg, 0, sizeof(msg));
    st.input_code_page = 936;
    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_SETTITLE_MSG) + 2;
    auto *set = reinterpret_cast<CONSOLE_SETTITLE_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    set->Unicode = FALSE;
    auto *bytes = reinterpret_cast<char *>(msg.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_SETTITLE_MSG));
    bytes[0] = static_cast<char>(0xCF);
    bytes[1] = static_cast<char>(0xB2);

    ASSERT(api_set_title(msg, st, sb, inp, bridge));
    ASSERT(st.title.size() == 1);
    ASSERT(st.title[0] == U'\u559c');
    return true;
}

bool test_regression_l3_mouse_info_rejects_short_message()
{
    miniio::io_msg msg{};
    console_state st;
    screen_buffer sb;
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};

    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETMOUSEINFO_MSG) - 1;
    ASSERT(api_l3_get_mouse_info(msg, st, sb, inp, bridge));
    ASSERT(msg.complete.IoStatus.Status == status_invalid_parameter);
    return true;
}

bool test_regression_l3_font_size_validation()
{
    miniio::io_msg msg{};
    console_state st;
    screen_buffer sb;
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};

    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETFONTSIZE_MSG) - 1;
    ASSERT(api_l3_get_font_size(msg, st, sb, inp, bridge));
    ASSERT(msg.complete.IoStatus.Status == status_invalid_parameter);

    std::memset(&msg, 0, sizeof(msg));
    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETFONTSIZE_MSG);
    auto *font = reinterpret_cast<CONSOLE_GETFONTSIZE_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    font->FontIndex = 1;
    font->FontSize = {9, 9};
    ASSERT(api_l3_get_font_size(msg, st, sb, inp, bridge));
    ASSERT(msg.complete.IoStatus.Status == status_invalid_parameter);
    ASSERT(font->FontSize.X == 0);
    ASSERT(font->FontSize.Y == 0);
    return true;
}

bool test_regression_l3_current_font_validation_and_maximum_window()
{
    miniio::io_msg msg{};
    console_state st;
    screen_buffer sb;
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};

    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_CURRENTFONT_MSG) - 1;
    ASSERT(api_l3_get_current_font(msg, st, sb, inp, bridge));
    ASSERT(msg.complete.IoStatus.Status == status_invalid_parameter);

    std::memset(&msg, 0, sizeof(msg));
    st.max_window_size = {132, 43};
    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_CURRENTFONT_MSG);
    auto *font = reinterpret_cast<CONSOLE_CURRENTFONT_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    font->MaximumWindow = TRUE;
    ASSERT(api_l3_get_current_font(msg, st, sb, inp, bridge));
    ASSERT(font->FontSize.X == 132);
    ASSERT(font->FontSize.Y == 43);
    return true;
}

bool test_regression_l3_set_display_mode_validation_and_size_output()
{
    miniio::io_msg msg{};
    console_state st;
    screen_buffer sb;
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};

    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_SETDISPLAYMODE_MSG) - 1;
    ASSERT(api_l3_set_display_mode(msg, st, sb, inp, bridge));
    ASSERT(msg.complete.IoStatus.Status == status_invalid_parameter);

    std::memset(&msg, 0, sizeof(msg));
    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_SETDISPLAYMODE_MSG);
    auto *mode = reinterpret_cast<CONSOLE_SETDISPLAYMODE_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    mode->dwFlags = 0;
    ASSERT(api_l3_set_display_mode(msg, st, sb, inp, bridge));
    ASSERT(msg.complete.IoStatus.Status == status_invalid_parameter);

    std::memset(&msg, 0, sizeof(msg));
    st.screen_buffer_size = {120, 30};
    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_SETDISPLAYMODE_MSG);
    mode = reinterpret_cast<CONSOLE_SETDISPLAYMODE_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    mode->dwFlags = CONSOLE_FULLSCREEN_MODE;
    mode->ScreenBufferDimensions = {1, 1};
    ASSERT(api_l3_set_display_mode(msg, st, sb, inp, bridge));
    ASSERT(msg.complete.IoStatus.Status == 0);
    ASSERT(mode->ScreenBufferDimensions.X == 120);
    ASSERT(mode->ScreenBufferDimensions.Y == 30);
    ASSERT(st.display_mode == CONSOLE_FULLSCREEN_MODE);
    return true;
}

bool test_regression_l3_get_display_mode_rejects_short_message()
{
    miniio::io_msg msg{};
    console_state st;
    screen_buffer sb;
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};

    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETDISPLAYMODE_MSG) - 1;
    ASSERT(api_l3_get_display_mode(msg, st, sb, inp, bridge));
    ASSERT(msg.complete.IoStatus.Status == status_invalid_parameter);

    std::memset(&msg, 0, sizeof(msg));
    st.display_mode = CONSOLE_FULLSCREEN_MODE;
    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETDISPLAYMODE_MSG);
    auto *mode = reinterpret_cast<CONSOLE_GETDISPLAYMODE_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    ASSERT(api_l3_get_display_mode(msg, st, sb, inp, bridge));
    ASSERT(mode->ModeFlags == CONSOLE_FULLSCREEN_MODE);
    return true;
}

bool test_regression_l3_add_alias_rejects_short_message()
{
    miniio::io_msg msg{};
    console_state st;

    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_ADDALIAS_MSG) - 1;
    ASSERT(api_l3_add_alias(msg, st, api_ctx.sb, api_ctx.inp, api_ctx.bridge));
    ASSERT(msg.complete.IoStatus.Status == status_invalid_parameter);
    return true;
}

bool test_regression_l3_get_alias_rejects_short_message()
{
    miniio::io_msg msg{};
    console_state st;

    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETALIAS_MSG) - 1;
    ASSERT(api_l3_get_alias(msg, st, api_ctx.sb, api_ctx.inp, api_ctx.bridge));
    ASSERT(msg.complete.IoStatus.Status == status_invalid_parameter);
    return true;
}

bool test_regression_l3_get_aliases_length_rejects_short_message()
{
    miniio::io_msg msg{};
    console_state st;

    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETALIASESLENGTH_MSG) - 1;
    ASSERT(api_l3_get_aliases_length(msg, st, api_ctx.sb, api_ctx.inp, api_ctx.bridge));
    ASSERT(msg.complete.IoStatus.Status == status_invalid_parameter);
    return true;
}

bool test_regression_l3_get_alias_exes_length_rejects_short_message()
{
    miniio::io_msg msg{};
    console_state st;

    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETALIASEXESLENGTH_MSG) - 1;
    ASSERT(api_l3_get_alias_exes_length(msg, st, api_ctx.sb, api_ctx.inp, api_ctx.bridge));
    ASSERT(msg.complete.IoStatus.Status == status_invalid_parameter);
    return true;
}

bool test_regression_l3_get_aliases_rejects_short_message()
{
    miniio::io_msg msg{};
    console_state st;

    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETALIASES_MSG) - 1;
    ASSERT(api_l3_get_aliases(msg, st, api_ctx.sb, api_ctx.inp, api_ctx.bridge));
    ASSERT(msg.complete.IoStatus.Status == status_invalid_parameter);
    return true;
}

bool test_regression_l3_get_alias_exes_rejects_short_message()
{
    miniio::io_msg msg{};
    console_state st;

    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETALIASEXES_MSG) - 1;
    ASSERT(api_l3_get_alias_exes(msg, st, api_ctx.sb, api_ctx.inp, api_ctx.bridge));
    ASSERT(msg.complete.IoStatus.Status == status_invalid_parameter);
    return true;
}

bool test_regression_l3_expunge_history_rejects_short_message_and_clears_history()
{
    miniio::io_msg msg{};
    console_state st;
    screen_buffer sb;
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};

    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_EXPUNGECOMMANDHISTORY_MSG) - 1;
    ASSERT(api_l3_expunge_history(msg, st, sb, inp, bridge));
    ASSERT(msg.complete.IoStatus.Status == status_invalid_parameter);

    bridge.test_cooked_append(U"cmd", 3);
    bridge.test_history_push();
    ASSERT(bridge.test_history_size() == 1);

    std::memset(&msg, 0, sizeof(msg));
    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_EXPUNGECOMMANDHISTORY_MSG);
    ASSERT(api_l3_expunge_history(msg, st, sb, inp, bridge));
    ASSERT(msg.complete.IoStatus.Status == 0);
    ASSERT(bridge.test_history_size() == 0);
    return true;
}

bool test_regression_l3_set_num_commands_validation_and_trim()
{
    miniio::io_msg msg{};
    console_state st;
    screen_buffer sb;
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};

    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_SETNUMBEROFCOMMANDS_MSG) - 1;
    ASSERT(api_l3_set_num_commands(msg, st, sb, inp, bridge));
    ASSERT(msg.complete.IoStatus.Status == status_invalid_parameter);

    bridge.test_cooked_append(U"one", 3);
    bridge.test_history_push();
    bridge.test_cooked_append(U"two", 3);
    bridge.test_history_push();
    bridge.test_cooked_append(U"three", 5);
    bridge.test_history_push();

    std::memset(&msg, 0, sizeof(msg));
    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_SETNUMBEROFCOMMANDS_MSG);
    auto *set = reinterpret_cast<CONSOLE_SETNUMBEROFCOMMANDS_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    set->NumCommands = 2;
    ASSERT(api_l3_set_num_commands(msg, st, sb, inp, bridge));
    ASSERT(msg.complete.IoStatus.Status == 0);
    ASSERT(st.history_buffer_size == 2);
    ASSERT(bridge.test_history_size() == 2);
    return true;
}

bool test_regression_l3_get_history_length_validation_and_bytes()
{
    miniio::io_msg msg{};
    console_state st;
    screen_buffer sb;
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};

    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETCOMMANDHISTORYLENGTH_MSG) - 1;
    ASSERT(api_l3_get_history_length(msg, st, sb, inp, bridge));
    ASSERT(msg.complete.IoStatus.Status == status_invalid_parameter);

    bridge.test_cooked_append(U"cmd1", 4);
    bridge.test_history_push();
    bridge.test_cooked_append(U"dir", 3);
    bridge.test_history_push();

    std::memset(&msg, 0, sizeof(msg));
    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETCOMMANDHISTORYLENGTH_MSG);
    auto *len = reinterpret_cast<CONSOLE_GETCOMMANDHISTORYLENGTH_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    len->Unicode = TRUE;
    ASSERT(api_l3_get_history_length(msg, st, sb, inp, bridge));
    ASSERT(msg.complete.IoStatus.Status == 0);
    ASSERT(len->CommandHistoryLength == (4 + 1 + 3 + 1) * sizeof(wchar_t));
    return true;
}

bool test_regression_l3_get_history_validation_output_size_and_serialization()
{
    miniio::io_msg msg{};
    console_state st;
    screen_buffer sb;
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};

    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETCOMMANDHISTORY_MSG) - 1;
    ASSERT(api_l3_get_history(msg, st, sb, inp, bridge));
    ASSERT(msg.complete.IoStatus.Status == status_invalid_parameter);

    bridge.test_cooked_append(U"cmd", 3);
    bridge.test_history_push();
    bridge.test_cooked_append(U"dir", 3);
    bridge.test_history_push();

    std::memset(&msg, 0, sizeof(msg));
    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETCOMMANDHISTORY_MSG);
    msg.descriptor.OutputSize = sizeof(CONSOLE_GETCOMMANDHISTORY_MSG) + 2 * sizeof(wchar_t);
    auto *hist = reinterpret_cast<CONSOLE_GETCOMMANDHISTORY_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    hist->Unicode = TRUE;
    ASSERT(api_l3_get_history(msg, st, sb, inp, bridge));
    ASSERT(msg.complete.IoStatus.Status == status_buffer_too_small);

    std::memset(&msg, 0, sizeof(msg));
    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETCOMMANDHISTORY_MSG);
    msg.descriptor.OutputSize = sizeof(CONSOLE_GETCOMMANDHISTORY_MSG) + (3 + 1 + 3 + 1) * sizeof(wchar_t);
    hist = reinterpret_cast<CONSOLE_GETCOMMANDHISTORY_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    hist->Unicode = TRUE;
    ASSERT(api_l3_get_history(msg, st, sb, inp, bridge));
    ASSERT(msg.complete.IoStatus.Status == 0);
    ASSERT(hist->CommandBufferLength == (3 + 1 + 3 + 1) * sizeof(wchar_t));

    auto *out = reinterpret_cast<const wchar_t *>(msg.body + sizeof(CONSOLE_MSG_HEADER) +
                                                  sizeof(CONSOLE_GETCOMMANDHISTORY_MSG));
    const auto first_command = std::wstring_view{out, 3};
    const auto second_command = std::wstring_view{out + 4, 3};
    ASSERT(first_command == L"cmd");
    ASSERT(out[3] == L'\0');
    ASSERT(second_command == L"dir");
    ASSERT(out[7] == L'\0');
    return true;
}

bool test_regression_l3_get_console_window_rejects_short_message()
{
    miniio::io_msg msg{};
    console_state st;
    screen_buffer sb;
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};

    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETCONSOLEWINDOW_MSG) - 1;
    ASSERT(api_l3_get_console_window(msg, st, sb, inp, bridge));
    ASSERT(msg.complete.IoStatus.Status == status_invalid_parameter);

    std::memset(&msg, 0, sizeof(msg));
    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETCONSOLEWINDOW_MSG);
    ASSERT(api_l3_get_console_window(msg, st, sb, inp, bridge));
    ASSERT(msg.complete.IoStatus.Status == 0);
    return true;
}

bool test_regression_l3_selection_info_validation_and_copy()
{
    miniio::io_msg msg{};
    console_state st;
    screen_buffer sb;
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};

    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETSELECTIONINFO_MSG) - 1;
    ASSERT(api_l3_get_selection_info(msg, st, sb, inp, bridge));
    ASSERT(msg.complete.IoStatus.Status == status_invalid_parameter);

    st.selection_info.dwFlags = CONSOLE_SELECTION_IN_PROGRESS;
    st.selection_info.dwSelectionAnchor = {2, 3};
    st.selection_info.srSelection = {1, 2, 5, 6};

    std::memset(&msg, 0, sizeof(msg));
    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETSELECTIONINFO_MSG);
    auto *selection = reinterpret_cast<CONSOLE_GETSELECTIONINFO_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    ASSERT(api_l3_get_selection_info(msg, st, sb, inp, bridge));
    ASSERT(msg.complete.IoStatus.Status == 0);
    ASSERT(selection->SelectionInfo.dwFlags == CONSOLE_SELECTION_IN_PROGRESS);
    ASSERT(selection->SelectionInfo.dwSelectionAnchor.X == 2);
    ASSERT(selection->SelectionInfo.srSelection.Right == 5);
    return true;
}

bool test_regression_l3_process_list_validation_and_output_size()
{
    miniio::io_msg msg{};
    console_state st;
    screen_buffer sb;
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};

    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETCONSOLEPROCESSLIST_MSG) - 1;
    ASSERT(api_l3_get_process_list(msg, st, sb, inp, bridge));
    ASSERT(msg.complete.IoStatus.Status == status_invalid_parameter);

    const DWORD processes[] = {11, 22, 33};
    bridge.set_process_list(processes);

    std::memset(&msg, 0, sizeof(msg));
    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETCONSOLEPROCESSLIST_MSG);
    msg.descriptor.OutputSize = sizeof(CONSOLE_GETCONSOLEPROCESSLIST_MSG) + 2 * sizeof(DWORD);
    auto *plist = reinterpret_cast<CONSOLE_GETCONSOLEPROCESSLIST_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    ASSERT(api_l3_get_process_list(msg, st, sb, inp, bridge));
    ASSERT(msg.complete.IoStatus.Status == 0);
    ASSERT(plist->dwProcessCount == 3);

    auto *out = reinterpret_cast<const DWORD *>(msg.body + sizeof(CONSOLE_MSG_HEADER) +
                                                sizeof(CONSOLE_GETCONSOLEPROCESSLIST_MSG));
    ASSERT(out[0] == 33);
    ASSERT(out[1] == 22);
    return true;
}

bool test_regression_l3_history_info_validation_get_set()
{
    miniio::io_msg msg{};
    console_state st;
    screen_buffer sb;
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};

    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_HISTORY_MSG) - 1;
    ASSERT(api_l3_get_history_info(msg, st, sb, inp, bridge));
    ASSERT(msg.complete.IoStatus.Status == status_invalid_parameter);

    std::memset(&msg, 0, sizeof(msg));
    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_HISTORY_MSG);
    auto *history = reinterpret_cast<CONSOLE_HISTORY_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    history->HistoryBufferSize = SHRT_MAX + 1u;
    ASSERT(api_l3_set_history_info(msg, st, sb, inp, bridge));
    ASSERT(msg.complete.IoStatus.Status == status_invalid_parameter);

    std::memset(&msg, 0, sizeof(msg));
    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_HISTORY_MSG);
    history = reinterpret_cast<CONSOLE_HISTORY_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    history->HistoryBufferSize = 7;
    history->NumberOfHistoryBuffers = 3;
    history->dwFlags = HISTORY_NO_DUP_FLAG;
    ASSERT(api_l3_set_history_info(msg, st, sb, inp, bridge));
    ASSERT(msg.complete.IoStatus.Status == 0);

    std::memset(&msg, 0, sizeof(msg));
    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_HISTORY_MSG);
    history = reinterpret_cast<CONSOLE_HISTORY_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    ASSERT(api_l3_get_history_info(msg, st, sb, inp, bridge));
    ASSERT(history->HistoryBufferSize == 7);
    ASSERT(history->NumberOfHistoryBuffers == 3);
    ASSERT(history->dwFlags == HISTORY_NO_DUP_FLAG);
    return true;
}

bool test_regression_l3_set_current_font_validation_and_store()
{
    miniio::io_msg msg{};
    console_state st;
    screen_buffer sb;
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};

    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_CURRENTFONT_MSG) - 1;
    ASSERT(api_l3_set_current_font(msg, st, sb, inp, bridge));
    ASSERT(msg.complete.IoStatus.Status == status_invalid_parameter);

    std::memset(&msg, 0, sizeof(msg));
    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_CURRENTFONT_MSG);
    auto *font = reinterpret_cast<CONSOLE_CURRENTFONT_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    font->FontIndex = 0;
    font->FontSize = {10, 20};
    font->FontFamily = TMPF_TRUETYPE;
    font->FontWeight = 700;
    std::wmemcpy(font->FaceName, L"Consolas", 8);
    ASSERT(api_l3_set_current_font(msg, st, sb, inp, bridge));
    ASSERT(msg.complete.IoStatus.Status == 0);
    ASSERT(st.font_size.X == 10);
    ASSERT(st.font_size.Y == 20);
    ASSERT(st.font_family == TMPF_TRUETYPE);
    ASSERT(st.font_weight == 700);
    const auto face_name = std::wstring_view{st.face_name, 8};
    ASSERT(face_name == L"Consolas");
    return true;
}

bool test_regression_raw_flush_clears_input_events()
{
    miniio::io_msg msg{};
    console_state st;
    screen_buffer main_sb;
    screen_buffer alt_sb;
    input_buffer inp;
    io_state io;
    pipe_bridge_testable bridge{inp, st, main_sb};
    api_router api{st, main_sb, alt_sb, inp, io, bridge};
    message_router router{io, bridge, api};

    INPUT_RECORD rec{};
    rec.EventType = KEY_EVENT;
    rec.Event.KeyEvent.bKeyDown = TRUE;
    rec.Event.KeyEvent.uChar.UnicodeChar = L'x';
    inp.write(&rec, 1);

    msg.descriptor.Function = CONSOLE_IO_RAW_FLUSH;
    ASSERT(router.on_message(msg));
    ASSERT(inp.available() == 0);
    ASSERT(msg.complete.IoStatus.Status == 0);
    return true;
}

bool test_regression_user_defined_router_matches_api_sorter_validation()
{
    miniio::io_msg msg{};
    console_state st;
    screen_buffer main_sb;
    screen_buffer alt_sb;
    input_buffer inp;
    io_state io;
    pipe_bridge_testable bridge{inp, st, main_sb};
    api_router api{st, main_sb, alt_sb, inp, io, bridge};

    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) - 1;
    ASSERT(api.handle_user_defined(msg));
    ASSERT(msg.complete.IoStatus.Status == status_illegal_function);

    std::memset(&msg, 0, sizeof(msg));
    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER);
    auto *hdr = reinterpret_cast<CONSOLE_MSG_HEADER *>(msg.body);
    hdr->ApiNumber = 0x04000000;
    hdr->ApiDescriptorSize = 0;
    ASSERT(api.handle_user_defined(msg));
    ASSERT(msg.complete.IoStatus.Status == status_illegal_function);

    std::memset(&msg, 0, sizeof(msg));
    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER);
    hdr = reinterpret_cast<CONSOLE_MSG_HEADER *>(msg.body);
    hdr->ApiNumber = static_cast<ULONG>(ConsolepGetNumberOfFonts);
    hdr->ApiDescriptorSize = 0;
    ASSERT(api.handle_user_defined(msg));
    ASSERT(msg.complete.IoStatus.Status == status_illegal_function);

    std::memset(&msg, 0, sizeof(msg));
    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETNUMBEROFFONTS_MSG);
    hdr = reinterpret_cast<CONSOLE_MSG_HEADER *>(msg.body);
    hdr->ApiNumber = static_cast<ULONG>(ConsolepGetNumberOfFonts);
    hdr->ApiDescriptorSize = sizeof(CONSOLE_GETNUMBEROFFONTS_MSG);
    ASSERT(api.handle_user_defined(msg));
    ASSERT(msg.complete.IoStatus.Status == status_not_implemented);
    return true;
}

bool test_regression_connect_disconnect_syncs_process_snapshot()
{
    miniio::io_msg msg{};
    console_state st;
    screen_buffer main_sb;
    screen_buffer alt_sb;
    input_buffer inp;
    io_state io;
    io.condrv_input = win32::duplicate_self();
    io.condrv_output = win32::duplicate_self();
    pipe_bridge_testable bridge{inp, st, main_sb};
    api_router api{st, main_sb, alt_sb, inp, io, bridge};
    message_router router{io, bridge, api};

    connect_completion completion = connect_completion::explicit_complete;
    msg.descriptor.Function = CONSOLE_IO_CONNECT;
    msg.descriptor.Process = 11;
    ASSERT(router.on_connect(msg, completion));
    ASSERT(completion == connect_completion::inline_complete);

    std::memset(&msg, 0, sizeof(msg));
    completion = connect_completion::explicit_complete;
    msg.descriptor.Function = CONSOLE_IO_CONNECT;
    msg.descriptor.Process = 22;
    ASSERT(router.on_connect(msg, completion));

    std::memset(&msg, 0, sizeof(msg));
    completion = connect_completion::explicit_complete;
    msg.descriptor.Function = CONSOLE_IO_CONNECT;
    msg.descriptor.Process = 33;
    ASSERT(router.on_connect(msg, completion));
    ASSERT(bridge.process_count() == 3);

    std::memset(&msg, 0, sizeof(msg));
    msg.descriptor.Function = CONSOLE_IO_DISCONNECT;
    msg.descriptor.Process = 11;
    ASSERT(router.on_message(msg));
    ASSERT(bridge.process_count() == 2);

    std::memset(&msg, 0, sizeof(msg));
    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETCONSOLEPROCESSLIST_MSG);
    msg.descriptor.OutputSize = sizeof(CONSOLE_GETCONSOLEPROCESSLIST_MSG) + 2 * sizeof(DWORD);
    auto *plist = reinterpret_cast<CONSOLE_GETCONSOLEPROCESSLIST_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    ASSERT(api_l3_get_process_list(msg, st, main_sb, inp, bridge));
    ASSERT(plist->dwProcessCount == 2);

    auto *out = reinterpret_cast<const DWORD *>(msg.body + sizeof(CONSOLE_MSG_HEADER) +
                                                sizeof(CONSOLE_GETCONSOLEPROCESSLIST_MSG));
    ASSERT(out[0] == 33);
    ASSERT(out[1] == 22);
    return true;
}

bool test_regression_create_object_rejects_malformed_or_unknown_type()
{
    miniio::io_msg msg{};
    io_state io;

    msg.descriptor.InputSize = sizeof(CD_CREATE_OBJECT_INFORMATION) - 1;
    ASSERT(io.handle_create_object(msg));
    ASSERT(msg.complete.IoStatus.Status == status_invalid_parameter);

    std::memset(&msg, 0, sizeof(msg));
    msg.descriptor.InputSize = sizeof(CD_CREATE_OBJECT_INFORMATION);
    auto *create = reinterpret_cast<CD_CREATE_OBJECT_INFORMATION *>(msg.body);
    create->ObjectType = 0xFFFF;
    ASSERT(io.handle_create_object(msg));
    ASSERT(msg.complete.IoStatus.Status == status_invalid_parameter);
    return true;
}

bool test_regression_new_output_screen_buffer_can_be_activated()
{
    miniio::io_msg msg{};
    console_state st;
    screen_buffer main_sb;
    screen_buffer alt_sb;
    input_buffer inp;
    io_state io;
    pipe_bridge_testable bridge{inp, st, main_sb};
    api_router router{st, main_sb, alt_sb, inp, io, bridge};

    io.output_id = 0x100;
    io.alternate_output_id = 0x200;

    msg.descriptor.Object = io.alternate_output_id;
    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER);
    ASSERT(router.dispatch_L2(msg, 2));
    ASSERT(msg.complete.IoStatus.Status == 0);
    ASSERT(router.alt_active);
    ASSERT(&router.active_screen_buffer() == &alt_sb);
    ASSERT(&bridge.active_screen_buffer() == &alt_sb);

    std::memset(&msg, 0, sizeof(msg));
    msg.descriptor.Object = io.output_id;
    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER);
    ASSERT(router.dispatch_L2(msg, 2));
    ASSERT(msg.complete.IoStatus.Status == 0);
    ASSERT(!router.alt_active);
    ASSERT(&router.active_screen_buffer() == &main_sb);
    ASSERT(&bridge.active_screen_buffer() == &main_sb);

    std::memset(&msg, 0, sizeof(msg));
    msg.descriptor.Object = io.alternate_output_id;
    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_MODE_MSG);
    const auto input_mode_before_set_output_mode = st.input_mode;
    auto *mode = reinterpret_cast<CONSOLE_MODE_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    mode->Mode = ENABLE_PROCESSED_OUTPUT | ENABLE_WRAP_AT_EOL_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    ASSERT(router.dispatch_L1(msg, 2));
    ASSERT(msg.complete.IoStatus.Status == 0);
    ASSERT((st.output_mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0);
    ASSERT(st.input_mode == input_mode_before_set_output_mode);
    return true;
}

bool test_regression_write_console_escape_sequence_without_vt_mode_updates_state()
{
    miniio::io_msg msg{};
    console_state st;
    screen_buffer sb;
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};

    msg.descriptor.InputSize = static_cast<ULONG>(sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_WRITECONSOLE_MSG) + 6);
    auto *write = reinterpret_cast<CONSOLE_WRITECONSOLE_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    write->Unicode = FALSE;
    std::memcpy(msg.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_WRITECONSOLE_MSG), "\x1b[?25l", 6);

    ASSERT(api_write_console(msg, st, sb, inp, bridge));
    ASSERT(msg.complete.IoStatus.Status == 0);
    ASSERT(st.cursor.position.X == 0);
    ASSERT(st.cursor.position.Y == 0);
    ASSERT(st.cursor.visible == false);
    return true;
}

bool test_regression_write_console_parser_sgr_updates_attributes()
{
    miniio::io_msg msg{};
    console_state st;
    screen_buffer sb;
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};

    constexpr char payload[] = "\x1b[31mX";
    msg.descriptor.InputSize =
        static_cast<ULONG>(sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_WRITECONSOLE_MSG) + sizeof(payload) - 1);
    auto *write = reinterpret_cast<CONSOLE_WRITECONSOLE_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    write->Unicode = FALSE;
    std::memcpy(msg.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_WRITECONSOLE_MSG), payload, sizeof(payload) - 1);

    ASSERT(api_write_console(msg, st, sb, inp, bridge));
    ASSERT(msg.complete.IoStatus.Status == 0);
    ASSERT(sb.at_u32({0, 0}) == U'X');
    ASSERT((sb.attr_at({0, 0}) & 0x0F) == 1);
    ASSERT((st.default_attributes & 0x0F) == 1);
    return true;
}

bool test_regression_write_console_parser_sgr_applies_params_in_order()
{
    miniio::io_msg msg{};
    console_state st;
    screen_buffer sb;
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};

    constexpr char payload[] = "\x1b[0;31mX";
    msg.descriptor.InputSize =
        static_cast<ULONG>(sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_WRITECONSOLE_MSG) + sizeof(payload) - 1);
    auto *write = reinterpret_cast<CONSOLE_WRITECONSOLE_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    write->Unicode = FALSE;
    std::memcpy(msg.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_WRITECONSOLE_MSG), payload, sizeof(payload) - 1);

    ASSERT(api_write_console(msg, st, sb, inp, bridge));
    ASSERT(msg.complete.IoStatus.Status == 0);
    ASSERT(sb.at_u32({0, 0}) == U'X');
    ASSERT((sb.attr_at({0, 0}) & 0x0F) == 1);
    ASSERT((st.default_attributes & 0x0F) == 1);
    return true;
}

bool test_regression_set_console_mode_validation()
{
    miniio::io_msg msg{};
    console_state st;
    screen_buffer sb;
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};

    auto *mode = reinterpret_cast<CONSOLE_MODE_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    mode->Mode = ENABLE_ECHO_INPUT;
    ASSERT(api_set_mode(msg, st, sb, inp, bridge, true));
    ASSERT(msg.complete.IoStatus.Status == status_invalid_parameter);
    ASSERT(st.input_mode == ENABLE_ECHO_INPUT);

    std::memset(&msg, 0, sizeof(msg));
    mode = reinterpret_cast<CONSOLE_MODE_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    mode->Mode = ENABLE_PROCESSED_OUTPUT | ENABLE_WRAP_AT_EOL_OUTPUT | 0x80000000u;
    ASSERT(api_set_mode(msg, st, sb, inp, bridge, false));
    ASSERT(msg.complete.IoStatus.Status == status_invalid_parameter);
    ASSERT(st.output_mode == (ENABLE_PROCESSED_OUTPUT | ENABLE_WRAP_AT_EOL_OUTPUT));

    return true;
}

// 回归: ExeLength=0 (无 exe 名)
bool test_regression_add_alias_zero_exe()
{
    console_state st;
    miniio::io_msg msg;

    mock_add_alias_msg(msg, L"", L"x", L"exit");
    api_l3_add_alias(msg, st, api_ctx.sb, api_ctx.inp, api_ctx.bridge);

    ASSERT(st.aliases.size() == 1);
    ASSERT(st.aliases[L"x"] == L"exit");
    return true;
}

// 回归: 端到端 — AddAlias 存储 → _expand_alias 查找
bool test_regression_alias_expand_after_store()
{
    console_state st;
    miniio::io_msg msg;

    mock_add_alias_msg(msg, L"cmd.exe", L"hello", L"echo hello");
    api_l3_add_alias(msg, st, api_ctx.sb, api_ctx.inp, api_ctx.bridge);
    ASSERT(st.aliases.size() == 1);

    screen_buffer sb;
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};
    bridge.test_cooked_append(U"hello", 5);
    bridge.test_expand_alias();

    ASSERT(bridge.test_get_cooked_buf() == U"echo hello");
    return true;
}

// 回归: 不匹配时 passthrough (确保消息布局错误不会意外匹配)
bool test_regression_alias_msg_wrong_key()
{
    console_state st;
    miniio::io_msg msg;

    mock_add_alias_msg(msg, L"cmd.exe", L"hello", L"echo hello");
    api_l3_add_alias(msg, st, api_ctx.sb, api_ctx.inp, api_ctx.bridge);
    ASSERT(st.aliases.size() == 1);

    screen_buffer sb;
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};
    bridge.test_cooked_append(U"world", 5);
    bridge.test_expand_alias();

    ASSERT(bridge.test_get_cooked_buf() == U"world");
    return true;
}

// 回归: ANSI 消息 (Unicode=0) 应被忽略
bool test_regression_add_alias_ansi_ignored()
{
    console_state st;
    miniio::io_msg msg;
    std::memset(&msg, 0, sizeof(msg));
    auto *hdr = reinterpret_cast<CONSOLE_MSG_HEADER *>(msg.body);
    hdr->ApiNumber = static_cast<ULONG>(ConsolepAddAlias);
    hdr->ApiDescriptorSize = sizeof(CONSOLE_ADDALIAS_MSG);
    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_ADDALIAS_MSG);

    auto *alias = reinterpret_cast<CONSOLE_ADDALIAS_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    alias->SourceLength = 0; // zero-length→skip
    alias->TargetLength = 0;
    alias->ExeLength = 0;
    alias->Unicode = FALSE; // ANSI with zero lengths

    api_l3_add_alias(msg, st, api_ctx.sb, api_ctx.inp, api_ctx.bridge);
    ASSERT(st.aliases.empty());
    return true;
}

// ── 辅助: 构造模拟 ConDrv GetAlias 消息 ──
void mock_get_alias_msg(miniio::io_msg &msg, const std::wstring &exe, const std::wstring &src)
{
    std::memset(&msg, 0, sizeof(msg));
    auto *hdr = reinterpret_cast<CONSOLE_MSG_HEADER *>(msg.body);
    hdr->ApiNumber = static_cast<ULONG>(ConsolepGetAlias);
    hdr->ApiDescriptorSize = sizeof(CONSOLE_GETALIAS_MSG);
    msg.descriptor.InputSize = static_cast<ULONG>(sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETALIAS_MSG) +
                                                  (exe.size() + src.size()) * sizeof(wchar_t));
    msg.descriptor.OutputSize =
        static_cast<ULONG>(sizeof(CONSOLE_GETALIAS_MSG) + exe.size() * sizeof(wchar_t) + 256 * sizeof(wchar_t));

    auto *r = reinterpret_cast<CONSOLE_GETALIAS_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    r->SourceLength = static_cast<USHORT>(src.size() * sizeof(wchar_t));
    r->ExeLength = static_cast<USHORT>(exe.size() * sizeof(wchar_t));
    r->Unicode = TRUE;

    BYTE *data = msg.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETALIAS_MSG);
    std::memcpy(data, exe.data(), exe.size() * sizeof(wchar_t));
    data += exe.size() * sizeof(wchar_t);
    std::memcpy(data, src.data(), src.size() * sizeof(wchar_t));
}

// 回归: GetAlias 跳过 ExeLength，用 SourceLength(字节) 取 key
bool test_regression_get_alias_skips_exe()
{
    console_state st;
    st.aliases[L"hello"] = L"echo hello";

    miniio::io_msg msg;
    mock_get_alias_msg(msg, L"cmd.exe", L"hello");

    api_l3_get_alias(msg, st, api_ctx.sb, api_ctx.inp, api_ctx.bridge);

    auto *r = reinterpret_cast<CONSOLE_GETALIAS_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    // TargetLength 包含结尾 NUL: "echo hello\0" = 11 wchars × 2 = 22
    ASSERT(r->TargetLength == 22);

    // 验证返回的 target 字符串被写回
    auto *data = msg.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETALIAS_MSG);
    auto *tgt_out = reinterpret_cast<const wchar_t *>(data + r->ExeLength);
    std::wstring_view tgt_sv{tgt_out, 10};
    ASSERT(tgt_sv == L"echo hello");
    return true;
}

// 回归: GetAlias 不存在的 key 返回 TargetLength=0
bool test_regression_get_alias_missing_key()
{
    console_state st;
    st.aliases[L"hello"] = L"echo hello";

    miniio::io_msg msg;
    mock_get_alias_msg(msg, L"cmd.exe", L"world");

    api_l3_get_alias(msg, st, api_ctx.sb, api_ctx.inp, api_ctx.bridge);

    auto *r = reinterpret_cast<CONSOLE_GETALIAS_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    ASSERT(r->TargetLength == 0);
    return true;
}

// 回归: GetAliases 的 AliasesBufferLength 应为字节数
bool test_regression_get_aliases_buffer_length_bytes()
{
    console_state st;
    st.aliases[L"ls"] = L"dir";
    st.aliases[L"cl"] = L"cls";

    miniio::io_msg msg;
    std::memset(&msg, 0, sizeof(msg));
    auto *hdr = reinterpret_cast<CONSOLE_MSG_HEADER *>(msg.body);
    hdr->ApiNumber = static_cast<ULONG>(ConsolepGetAliases);
    hdr->ApiDescriptorSize = sizeof(CONSOLE_GETALIASES_MSG);
    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETALIASES_MSG);
    msg.descriptor.OutputSize = sizeof(CONSOLE_GETALIASES_MSG) + 256 * sizeof(wchar_t);

    auto *r = reinterpret_cast<CONSOLE_GETALIASES_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    r->Unicode = TRUE;

    api_l3_get_aliases(msg, st, api_ctx.sb, api_ctx.inp, api_ctx.bridge);

    // 序列化: "ls\0dir\0cl\0cls\0" = (2+1+3+1) + (2+1+3+1) = 14 wchars
    ULONG expected_wchars = 14;
    ASSERT(r->AliasesBufferLength == expected_wchars * sizeof(wchar_t));
    return true;
}

// 回归: GetAliasesLength ANSI 返回字节数 (不含 *sizeof(wchar_t))
bool test_regression_get_aliases_length_ansi()
{
    console_state st;
    st.aliases[L"x"] = L"exit";

    miniio::io_msg msg;
    std::memset(&msg, 0, sizeof(msg));
    auto *hdr = reinterpret_cast<CONSOLE_MSG_HEADER *>(msg.body);
    hdr->ApiNumber = static_cast<ULONG>(ConsolepGetAliasesLength);
    hdr->ApiDescriptorSize = sizeof(CONSOLE_GETALIASESLENGTH_MSG);
    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETALIASESLENGTH_MSG);

    auto *r = reinterpret_cast<CONSOLE_GETALIASESLENGTH_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    r->Unicode = FALSE;

    api_l3_get_aliases_length(msg, st, api_ctx.sb, api_ctx.inp, api_ctx.bridge);

    // "x=exit\0" = 1 + 1 + 4 + 1 = 7 字节
    ASSERT(r->AliasesLength == 7);
    return true;
}

// 回归: GetCommandHistoryLength 返回 0 (ConPTY 不暴露历史)
bool test_regression_get_history_length_zero()
{
    miniio::io_msg msg;
    std::memset(&msg, 0, sizeof(msg));
    auto *hdr = reinterpret_cast<CONSOLE_MSG_HEADER *>(msg.body);
    hdr->ApiNumber = static_cast<ULONG>(ConsolepGetCommandHistoryLength);
    hdr->ApiDescriptorSize = sizeof(CONSOLE_GETCOMMANDHISTORYLENGTH_MSG);
    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETCOMMANDHISTORYLENGTH_MSG);

    console_state st;
    api_l3_get_history_length(msg, st, api_ctx.sb, api_ctx.inp, api_ctx.bridge);

    auto *r = reinterpret_cast<CONSOLE_GETCOMMANDHISTORYLENGTH_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    ASSERT(r->CommandHistoryLength == 0);
    return true;
}

// 回归: GetCommandHistory 返回 CommandBufferLength=0 (ConPTY 不暴露历史)
bool test_regression_get_history_zero()
{
    miniio::io_msg msg;
    std::memset(&msg, 0, sizeof(msg));
    auto *hdr = reinterpret_cast<CONSOLE_MSG_HEADER *>(msg.body);
    hdr->ApiNumber = static_cast<ULONG>(ConsolepGetCommandHistory);
    hdr->ApiDescriptorSize = sizeof(CONSOLE_GETCOMMANDHISTORY_MSG);
    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETCOMMANDHISTORY_MSG);
    msg.descriptor.OutputSize = sizeof(CONSOLE_GETCOMMANDHISTORY_MSG);

    console_state st;
    api_l3_get_history(msg, st, api_ctx.sb, api_ctx.inp, api_ctx.bridge);

    auto *r = reinterpret_cast<CONSOLE_GETCOMMANDHISTORY_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    ASSERT(r->CommandBufferLength == 0);
    return true;
}

// 回归: GetTitle 的 TitleLength 应为字节数
bool test_regression_get_title_length_bytes()
{
    console_state st;
    std::u32string u32title;
    convert_utf16_to_u32(std::wstring_view{L"test"}, u32title);
    st.title = u32title;

    miniio::io_msg msg;
    std::memset(&msg, 0, sizeof(msg));
    auto *hdr = reinterpret_cast<CONSOLE_MSG_HEADER *>(msg.body);
    hdr->ApiNumber = static_cast<ULONG>(ConsolepGetTitle);
    hdr->ApiDescriptorSize = sizeof(CONSOLE_GETTITLE_MSG);
    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETTITLE_MSG);
    msg.descriptor.OutputSize = sizeof(CONSOLE_GETTITLE_MSG) + 5 * sizeof(wchar_t);

    auto *r = reinterpret_cast<CONSOLE_GETTITLE_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    r->Unicode = TRUE;
    r->Original = FALSE;

    api_get_title(msg, st, api_ctx.sb, api_ctx.inp, api_ctx.bridge);

    // "test" = 4 wchars × 2 = 8 字节
    ASSERT(r->TitleLength == 8);
    return true;
}

// ── 辅助: 构造模拟 ConDrv AddAlias ANSI 消息 ──
void mock_add_alias_msg_ansi(miniio::io_msg &msg, const std::string &exe, const std::string &src,
                             const std::string &tgt)
{
    std::memset(&msg, 0, sizeof(msg));
    auto *hdr = reinterpret_cast<CONSOLE_MSG_HEADER *>(msg.body);
    hdr->ApiNumber = static_cast<ULONG>(ConsolepAddAlias);
    hdr->ApiDescriptorSize = sizeof(CONSOLE_ADDALIAS_MSG);
    msg.descriptor.InputSize = static_cast<ULONG>(sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_ADDALIAS_MSG) +
                                                  exe.size() + src.size() + tgt.size());

    auto *alias = reinterpret_cast<CONSOLE_ADDALIAS_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    alias->SourceLength = static_cast<USHORT>(src.size());
    alias->TargetLength = static_cast<USHORT>(tgt.size());
    alias->ExeLength = static_cast<USHORT>(exe.size());
    alias->Unicode = FALSE;

    BYTE *data = msg.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_ADDALIAS_MSG);
    std::memcpy(data, exe.data(), exe.size());
    data += exe.size();
    std::memcpy(data, src.data(), src.size());
    data += src.size();
    std::memcpy(data, tgt.data(), tgt.size());
}

// 回归: AddAlias ANSI 消息正确解析 (ASCII 字符集)
bool test_regression_add_alias_ansi_ascii()
{
    console_state st;
    miniio::io_msg msg;

    mock_add_alias_msg_ansi(msg, "cmd.exe", "hello", "echo hello");
    api_l3_add_alias(msg, st, api_ctx.sb, api_ctx.inp, api_ctx.bridge);

    ASSERT(st.aliases.size() == 1);
    ASSERT(st.aliases[L"hello"] == L"echo hello");
    return true;
}

// 回归: GetAlias ANSI 返回 target 为 char* 字节
bool test_regression_get_alias_ansi_output()
{
    console_state st;
    st.aliases[L"ls"] = L"dir";

    miniio::io_msg msg;
    std::memset(&msg, 0, sizeof(msg));
    auto *hdr = reinterpret_cast<CONSOLE_MSG_HEADER *>(msg.body);
    hdr->ApiNumber = static_cast<ULONG>(ConsolepGetAlias);
    hdr->ApiDescriptorSize = sizeof(CONSOLE_GETALIAS_MSG);

    std::string exe_a = "cmd.exe";
    std::string src_a = "ls";
    msg.descriptor.InputSize =
        static_cast<ULONG>(sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETALIAS_MSG) + exe_a.size() + src_a.size());
    msg.descriptor.OutputSize = static_cast<ULONG>(sizeof(CONSOLE_GETALIAS_MSG) + exe_a.size() + 64);
    auto *r = reinterpret_cast<CONSOLE_GETALIAS_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    r->SourceLength = static_cast<USHORT>(src_a.size());
    r->ExeLength = static_cast<USHORT>(exe_a.size());
    r->Unicode = FALSE;

    BYTE *data = msg.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETALIAS_MSG);
    std::memcpy(data, exe_a.data(), exe_a.size());
    data += exe_a.size();
    std::memcpy(data, src_a.data(), src_a.size());

    api_l3_get_alias(msg, st, api_ctx.sb, api_ctx.inp, api_ctx.bridge);

    // TargetLength 包含结尾 NUL: "dir\0" = 4 字节
    ASSERT(r->TargetLength == 4);
    // 验证写回的 target 字符串
    auto *tgt_out = reinterpret_cast<const char *>(msg.body + sizeof(CONSOLE_MSG_HEADER) +
                                                   sizeof(CONSOLE_GETALIAS_MSG) + exe_a.size());
    std::string tgt_str{tgt_out, 3};
    ASSERT(tgt_str == "dir");
    return true;
}

// 回归: GetAliases ANSI 输出 char* 序列
bool test_regression_get_aliases_ansi_output()
{
    console_state st;
    st.aliases[L"x"] = L"exit";

    miniio::io_msg msg;
    std::memset(&msg, 0, sizeof(msg));
    auto *hdr = reinterpret_cast<CONSOLE_MSG_HEADER *>(msg.body);
    hdr->ApiNumber = static_cast<ULONG>(ConsolepGetAliases);
    hdr->ApiDescriptorSize = sizeof(CONSOLE_GETALIASES_MSG);
    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETALIASES_MSG);
    msg.descriptor.OutputSize = sizeof(CONSOLE_GETALIASES_MSG) + 256;

    auto *r = reinterpret_cast<CONSOLE_GETALIASES_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    r->Unicode = FALSE;

    api_l3_get_aliases(msg, st, api_ctx.sb, api_ctx.inp, api_ctx.bridge);

    // 序列化: "x=exit\0" = 1 + 1 + 4 + 1 = 7 字节
    ASSERT(r->AliasesBufferLength == 7);
    auto *out = reinterpret_cast<const char *>(msg.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETALIASES_MSG));
    std::string alias_str{out, 6};
    ASSERT(alias_str == "x=exit");
    return true;
}

// 回归: GetTitle ANSI 输出 char*
bool test_regression_get_title_ansi_output()
{
    console_state st;
    std::u32string u32title;
    convert_utf16_to_u32(std::wstring_view{L"cmd"}, u32title);
    st.title = u32title;

    miniio::io_msg msg;
    std::memset(&msg, 0, sizeof(msg));
    auto *hdr = reinterpret_cast<CONSOLE_MSG_HEADER *>(msg.body);
    hdr->ApiNumber = static_cast<ULONG>(ConsolepGetTitle);
    hdr->ApiDescriptorSize = sizeof(CONSOLE_GETTITLE_MSG);
    msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETTITLE_MSG);
    msg.descriptor.OutputSize = sizeof(CONSOLE_GETTITLE_MSG) + 4;

    auto *r = reinterpret_cast<CONSOLE_GETTITLE_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    r->Unicode = FALSE;
    r->Original = FALSE;

    api_get_title(msg, st, api_ctx.sb, api_ctx.inp, api_ctx.bridge);

    // "cmd" = 3 字节
    ASSERT(r->TitleLength == 3);
    auto *out = reinterpret_cast<const char *>(msg.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETTITLE_MSG));
    std::string title_str{out, 3};
    ASSERT(title_str == "cmd");
    return true;
}

#include "pipe_bridge.hpp"

bool test_alias_expand_simple_match()
{
    console_state st;
    st.aliases[L"hello"] = L"echo hello";

    screen_buffer sb;
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};
    bridge.test_cooked_append(U"hello", 5);
    bridge.test_expand_alias();

    ASSERT(bridge.test_get_cooked_buf() == U"echo hello");
    return true;
}

bool test_alias_expand_with_trailing_args()
{
    console_state st;
    st.aliases[L"gs"] = L"git status";

    screen_buffer sb;
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};
    bridge.test_cooked_append(U"gs --short", 10);
    bridge.test_expand_alias();

    ASSERT(bridge.test_get_cooked_buf() == U"git status --short");
    return true;
}

bool test_alias_expand_no_match_passthrough()
{
    console_state st;
    st.aliases[L"hello"] = L"echo hello";

    screen_buffer sb;
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};
    bridge.test_cooked_append(U"world", 5);
    bridge.test_expand_alias();

    ASSERT(bridge.test_get_cooked_buf() == U"world");
    return true;
}

bool test_alias_expand_empty_aliases()
{
    console_state st;

    screen_buffer sb;
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};
    bridge.test_cooked_append(U"anything", 8);
    bridge.test_expand_alias();

    ASSERT(bridge.test_get_cooked_buf() == U"anything");
    return true;
}

bool test_alias_expand_single_word()
{
    console_state st;
    st.aliases[L"x"] = L"exit";

    screen_buffer sb;
    input_buffer inp;
    pipe_bridge_testable bridge{inp, st, sb};
    bridge.test_cooked_append(U"x", 1);
    bridge.test_expand_alias();

    ASSERT(bridge.test_get_cooked_buf() == U"exit");
    return true;
}

// ═══════════════════════════════════════════════════════
// 其他字段
// ═══════════════════════════════════════════════════════

bool test_font_info()
{
    console_state st;
    ASSERT(st.font_index == 0);
    ASSERT(st.font_size.X == 8);
    ASSERT(st.font_size.Y == 12);
    ASSERT(st.font_weight == 400);
    // face_name 是 WCHAR[32]
    ASSERT(wcscmp(st.face_name, L"Consolas") == 0);
    return true;
}

bool test_mouse_buttons()
{
    console_state st;
    st.mouse_buttons = 5;
    ASSERT(st.mouse_buttons == 5);
    return true;
}

bool test_display_mode()
{
    console_state st;
    st.display_mode = 1; // fullscreen
    ASSERT(st.display_mode == 1);
    return true;
}

bool test_default_attributes()
{
    console_state st;
    ASSERT(st.default_attributes == 0x07);
    st.default_attributes = 0x1F;
    ASSERT(st.default_attributes == 0x1F);
    return true;
}

bool test_dec_line_drawing()
{
    console_state st;
    ASSERT(st.dec_line_drawing_mode == false);
    st.dec_line_drawing_mode = true;
    ASSERT(st.dec_line_drawing_mode == true);
    return true;
}

bool test_dec_to_unicode()
{
    console_state st;
    // 测试 DEC 特殊图形字符映射
    // 'j' (0x6A) → U+2518 (┘)
    ASSERT(st.dec_to_unicode(0x6A) == 0x2518);
    // 'k' (0x6B) → U+2510 (┐)
    ASSERT(st.dec_to_unicode(0x6B) == 0x2510);
    // 'l' (0x6C) → U+250C (┌)
    ASSERT(st.dec_to_unicode(0x6C) == 0x250C);
    // 'm' (0x6D) → U+2514 (└)
    ASSERT(st.dec_to_unicode(0x6D) == 0x2514);
    // 'n' (0x6E) → U+253C (┼)
    ASSERT(st.dec_to_unicode(0x6E) == 0x253C);
    // 'x' (0x78) → U+2502 (│)
    ASSERT(st.dec_to_unicode(0x78) == 0x2502);
    return true;
}

// ═══════════════════════════════════════════════════════
// Test Runner
// ═══════════════════════════════════════════════════════

int main()
{
    std::wcout << L"=== console_state Tests ===" << std::endl;

    RUN_TEST(test_cursor_default, L"Cursor default");
    RUN_TEST(test_cursor_visible, L"Cursor visible");
    RUN_TEST(test_cursor_save_restore, L"Cursor save/restore");

    RUN_TEST(test_tab_stops_init, L"Tab init");
    RUN_TEST(test_tab_set_and_clear, L"Tab set/clear");
    RUN_TEST(test_tab_stop_next, L"Tab next");
    RUN_TEST(test_tab_stop_prev, L"Tab prev");

    RUN_TEST(test_default_mode, L"Default mode");
    RUN_TEST(test_mode_bits, L"Mode bits");
    RUN_TEST(test_codepage, L"Codepage");

    RUN_TEST(test_title_initial, L"Title initial");
    RUN_TEST(test_title_set, L"Title set");
    RUN_TEST(test_original_title, L"Original title");

    RUN_TEST(test_history_settings_default, L"History settings default");

    RUN_TEST(test_alias_add_and_find, L"Alias add/find");
    RUN_TEST(test_alias_empty, L"Alias empty");
    RUN_TEST(test_regression_get_console_input_nowait_empty, L"GetConsoleInput NOWAIT empty");
    RUN_TEST(test_regression_get_console_input_waits_when_empty, L"GetConsoleInput waits when empty");
    RUN_TEST(test_regression_get_console_input_ready_event, L"GetConsoleInput ready event");
    RUN_TEST(test_regression_get_console_input_output_size_excludes_header,
             L"GetConsoleInput OutputSize excludes header");
    RUN_TEST(test_regression_get_console_input_output_size_without_record,
             L"GetConsoleInput OutputSize without record");
    RUN_TEST(test_regression_get_console_input_uses_completion_buffer_for_large_output,
             L"GetConsoleInput large completion buffer");
    RUN_TEST(test_regression_signal_shutdown_requests_exit_only_without_pending,
             L"Signal shutdown requests exit only without pending");
    RUN_TEST(test_regression_get_console_input_rejects_invalid_flags, L"GetConsoleInput rejects invalid flags");
    RUN_TEST(test_regression_raw_write_decodes_output_codepage, L"RawWrite decodes output codepage");
    RUN_TEST(test_regression_raw_read_completion_writes_only_bytes, L"RawRead writes only bytes");
    RUN_TEST(test_regression_raw_read_completion_respects_output_size, L"RawRead respects OutputSize");
    RUN_TEST(test_regression_read_console_a_uses_input_codepage, L"ReadConsoleA uses input codepage");
    RUN_TEST(test_regression_read_console_initial_bytes_check_output_capacity, L"ReadConsole initial bytes capacity");
    RUN_TEST(test_regression_write_console_rejects_short_message, L"WriteConsole rejects short message");
    RUN_TEST(test_regression_write_console_w_reports_complete_utf16_units, L"WriteConsoleW reports complete UTF16");
    RUN_TEST(test_regression_write_console_answers_terminal_cpr_and_da_queries,
             L"WriteConsole answers CPR/DA terminal queries");
    RUN_TEST(test_regression_write_console_cpr_response_uses_viewport_relative_cursor,
             L"WriteConsole CPR uses viewport-relative cursor");
    RUN_TEST(test_regression_deprecated_l1_returns_not_implemented, L"L1 deprecated returns not implemented");
    RUN_TEST(test_regression_get_langid_matches_original_gate, L"GetConsoleLangId original gate");
    RUN_TEST(test_regression_fill_console_output_a_uses_output_codepage, L"FillConsoleOutputA uses output codepage");
    RUN_TEST(test_regression_fill_console_output_rejects_short_message, L"FillConsoleOutput rejects short message");
    RUN_TEST(test_regression_fill_console_output_attr_preserves_current_attr,
             L"FillConsoleOutputAttribute preserves current attr");
    RUN_TEST(test_regression_ctrl_event_rejects_short_message, L"GenerateConsoleCtrlEvent rejects short message");
    RUN_TEST(test_regression_set_console_cp_rejects_short_message, L"SetConsoleCP rejects short message");
    RUN_TEST(test_regression_set_console_cp_updates_selected_codepage, L"SetConsoleCP updates selected codepage");
    RUN_TEST(test_regression_cursor_info_rejects_short_messages, L"CursorInfo rejects short messages");
    RUN_TEST(test_regression_get_screen_buffer_info_rejects_short_message,
             L"GetScreenBufferInfo rejects short message");
    RUN_TEST(test_regression_set_screen_buffer_info_validation, L"SetScreenBufferInfo validation");
    RUN_TEST(test_regression_set_screen_buffer_size_rejects_short_message,
             L"SetScreenBufferSize rejects short message");
    RUN_TEST(test_regression_set_cursor_position_rejects_short_message, L"SetCursorPosition rejects short message");
    RUN_TEST(test_regression_largest_window_rejects_short_message, L"LargestWindow rejects short message");
    RUN_TEST(test_regression_scroll_screen_buffer_validation_and_ansi_fill,
             L"ScrollScreenBuffer validation and ANSI fill");
    RUN_TEST(test_regression_set_text_attribute_rejects_short_message, L"SetTextAttribute rejects short message");
    RUN_TEST(test_regression_set_window_info_rejects_short_message, L"SetWindowInfo rejects short message");
    RUN_TEST(test_viewport_set_window_info_absolute_updates_origin_without_resizing_buffer,
             L"Viewport SetWindowInfo absolute");
    RUN_TEST(test_viewport_set_window_info_relative_offsets_current_rect, L"Viewport SetWindowInfo relative");
    RUN_TEST(test_viewport_set_cursor_position_snaps_cursor_into_view, L"Viewport SetCursorPosition snaps");
    RUN_TEST(test_viewport_set_screen_buffer_info_resizes_view_without_moving_origin,
             L"Viewport SetScreenBufferInfo size only");
    RUN_TEST(test_viewport_state_is_owned_by_each_screen_buffer, L"Viewport per-screen-buffer ownership");
    RUN_TEST(test_viewport_vt_cursor_position_updates_buffer_coordinates,
             L"Viewport VT cursor updates buffer coordinates");
    RUN_TEST(test_viewport_vt_scroll_is_clipped_to_visible_window, L"Viewport VT scroll clips to visible window");
    RUN_TEST(test_viewport_vt_insert_delete_lines_start_at_cursor_row,
             L"Viewport VT insert/delete lines clips to visible window");
    RUN_TEST(test_viewport_vt_text_wraps_inside_visible_window, L"Viewport VT text wraps inside visible window");
    RUN_TEST(test_viewport_vt_line_feed_scrolls_visible_window, L"Viewport VT line feed scrolls visible window");
    RUN_TEST(test_viewport_vt_character_editing_is_clipped_to_visible_window,
             L"Viewport VT character editing clips to visible window");
    RUN_TEST(test_viewport_vt_reverse_index_scrolls_visible_window,
             L"Viewport VT reverse index scrolls visible window");
    RUN_TEST(test_viewport_vt_tabs_are_viewport_relative, L"Viewport VT tabs are viewport-relative");
    RUN_TEST(test_viewport_vt_scrolling_region_is_viewport_relative,
             L"Viewport VT scrolling region is viewport-relative");
    RUN_TEST(test_regression_read_output_string_output_size_and_linear_read,
             L"ReadOutputString output size and linear read");
    RUN_TEST(test_regression_write_console_input_a_uses_input_codepage, L"WriteConsoleInputA uses input codepage");
    RUN_TEST(test_regression_write_console_output_validation_and_clipping,
             L"WriteConsoleOutput validation and clipping");
    RUN_TEST(test_regression_write_output_string_linear_and_ansi_count, L"WriteOutputString linear and ANSI count");
    RUN_TEST(test_regression_read_console_output_output_size_and_clipping,
             L"ReadConsoleOutput output size and clipping");
    RUN_TEST(test_regression_get_title_output_size_limits_copy, L"GetTitle output size limits copy");
    RUN_TEST(test_regression_set_title_a_uses_input_codepage, L"SetTitleA uses input codepage");
    RUN_TEST(test_regression_l3_mouse_info_rejects_short_message, L"L3 MouseInfo rejects short message");
    RUN_TEST(test_regression_l3_font_size_validation, L"L3 FontSize validation");
    RUN_TEST(test_regression_l3_current_font_validation_and_maximum_window,
             L"L3 CurrentFont validation and maximum window");
    RUN_TEST(test_regression_l3_set_display_mode_validation_and_size_output,
             L"L3 SetDisplayMode validation and size output");
    RUN_TEST(test_regression_l3_get_display_mode_rejects_short_message, L"L3 GetDisplayMode rejects short message");
    RUN_TEST(test_regression_l3_add_alias_rejects_short_message, L"L3 AddAlias rejects short message");
    RUN_TEST(test_regression_l3_get_alias_rejects_short_message, L"L3 GetAlias rejects short message");
    RUN_TEST(test_regression_l3_get_aliases_length_rejects_short_message, L"L3 GetAliasesLength rejects short message");
    RUN_TEST(test_regression_l3_get_alias_exes_length_rejects_short_message,
             L"L3 GetAliasExesLength rejects short message");
    RUN_TEST(test_regression_l3_get_aliases_rejects_short_message, L"L3 GetAliases rejects short message");
    RUN_TEST(test_regression_l3_get_alias_exes_rejects_short_message, L"L3 GetAliasExes rejects short message");
    RUN_TEST(test_regression_l3_expunge_history_rejects_short_message_and_clears_history,
             L"L3 ExpungeHistory validation and clear");
    RUN_TEST(test_regression_l3_set_num_commands_validation_and_trim, L"L3 SetNumberOfCommands validation and trim");
    RUN_TEST(test_regression_l3_get_history_length_validation_and_bytes, L"L3 GetHistoryLength validation and bytes");
    RUN_TEST(test_regression_l3_get_history_validation_output_size_and_serialization,
             L"L3 GetHistory validation and serialization");
    RUN_TEST(test_regression_l3_get_console_window_rejects_short_message, L"L3 GetConsoleWindow rejects short message");
    RUN_TEST(test_regression_l3_selection_info_validation_and_copy, L"L3 SelectionInfo validation and copy");
    RUN_TEST(test_regression_l3_process_list_validation_and_output_size, L"L3 ProcessList validation and output size");
    RUN_TEST(test_regression_l3_history_info_validation_get_set, L"L3 HistoryInfo validation get/set");
    RUN_TEST(test_regression_l3_set_current_font_validation_and_store, L"L3 SetCurrentFont validation and store");
    RUN_TEST(test_regression_raw_flush_clears_input_events, L"RawFlush clears input events");
    RUN_TEST(test_regression_user_defined_router_matches_api_sorter_validation, L"USER_DEFINED router validation");
    RUN_TEST(test_regression_connect_disconnect_syncs_process_snapshot, L"CONNECT/DISCONNECT sync process snapshot");
    RUN_TEST(test_regression_create_object_rejects_malformed_or_unknown_type,
             L"CREATE_OBJECT rejects malformed or unknown type");
    RUN_TEST(test_regression_new_output_screen_buffer_can_be_activated, L"New output screen buffer can be activated");
    RUN_TEST(test_regression_write_console_escape_sequence_without_vt_mode_updates_state,
             L"WriteConsole escape sequence updates state without VT mode");
    RUN_TEST(test_regression_write_console_parser_sgr_updates_attributes,
             L"WriteConsole parser SGR updates attributes");
    RUN_TEST(test_regression_write_console_parser_sgr_applies_params_in_order,
             L"WriteConsole parser SGR applies params in order");
    RUN_TEST(test_regression_set_console_mode_validation, L"SetConsoleMode validation");
    RUN_TEST(test_regression_add_alias_msg_layout, L"Alias msg layout (Exe+Src+Tgt)");
    RUN_TEST(test_regression_add_alias_zero_exe, L"Alias msg zero exe");
    RUN_TEST(test_regression_alias_expand_after_store, L"Alias expand after store");
    RUN_TEST(test_regression_alias_msg_wrong_key, L"Alias msg wrong key passthrough");
    RUN_TEST(test_regression_add_alias_ansi_ignored, L"Alias ANSI ignored");
    RUN_TEST(test_regression_get_alias_skips_exe, L"GetAlias skips Exe+uses bytes");
    RUN_TEST(test_regression_get_alias_missing_key, L"GetAlias missing key");
    RUN_TEST(test_regression_get_aliases_buffer_length_bytes, L"GetAliases length bytes");
    RUN_TEST(test_regression_get_aliases_length_ansi, L"GetAliasesLength ANSI bytes");
    RUN_TEST(test_regression_get_history_length_zero, L"GetHistoryLength returns 0");
    RUN_TEST(test_regression_get_history_zero, L"GetHistory returns 0");
    RUN_TEST(test_regression_get_title_length_bytes, L"GetTitle length in bytes");
    RUN_TEST(test_regression_add_alias_ansi_ascii, L"AddAlias ANSI ASCII parsing");
    RUN_TEST(test_regression_get_alias_ansi_output, L"GetAlias ANSI output char*");
    RUN_TEST(test_regression_get_aliases_ansi_output, L"GetAliases ANSI output char*");
    RUN_TEST(test_regression_get_title_ansi_output, L"GetTitle ANSI output char*");
    RUN_TEST(test_alias_expand_simple_match, L"Alias expand simple");
    RUN_TEST(test_alias_expand_with_trailing_args, L"Alias expand + args");
    RUN_TEST(test_alias_expand_no_match_passthrough, L"Alias expand no match");
    RUN_TEST(test_alias_expand_empty_aliases, L"Alias expand empty map");
    RUN_TEST(test_alias_expand_single_word, L"Alias expand single word");

    RUN_TEST(test_font_info, L"Font info");
    RUN_TEST(test_mouse_buttons, L"Mouse buttons");
    RUN_TEST(test_display_mode, L"Display mode");
    RUN_TEST(test_default_attributes, L"Default attrs");
    RUN_TEST(test_dec_line_drawing, L"DEC line drawing");
    RUN_TEST(test_dec_to_unicode, L"dec_to_unicode");

    std::wcout << L"  " << tests_passed << L" passed, " << tests_failed << L" failed, " << (tests_passed + tests_failed)
               << L" total." << std::endl;
    return tests_failed > 0 ? 1 : 0;
}
