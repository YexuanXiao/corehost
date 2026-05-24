// ── tests/test_console_state.cpp ──────────────────────
// 控制台状态单元测试 (console_state.hpp)
//
// 覆盖: 光标、Tab 停靠位、模式、标题、别名、历史
#include "test_common.hpp"
#include "conpty/console_state.hpp"

using namespace conpty;

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
    ASSERT(st.input_mode != 0);
    ASSERT(st.output_mode != 0);
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

// ═══════════════════════════════════════════════════════
// 命令历史
// ═══════════════════════════════════════════════════════

bool test_history_default()
{
    console_state st;
    ASSERT(st.command_history.empty());
    ASSERT(st.history_buffer_size == 50);
    ASSERT(st.history_num_buffers == 4);
    return true;
}

bool test_history_add()
{
    console_state st;
    st.command_history.push_back(U"cmd1");
    st.command_history.push_back(U"cmd2");
    ASSERT(st.command_history.size() == 2);
    ASSERT(st.command_history[0] == U"cmd1");
    ASSERT(st.command_history[1] == U"cmd2");
    return true;
}

bool test_history_clear()
{
    console_state st;
    st.command_history.push_back(U"test");
    st.command_history.clear();
    ASSERT(st.command_history.empty());
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

#include "conpty/api_handlers.hpp"
#include "os/Console/conmsgl3.h"

// 辅助: 构造模拟 ConDrv AddAlias 消息 (Unicode)
void mock_add_alias_msg(miniio::io_msg &msg, const std::wstring &exe, const std::wstring &src, const std::wstring &tgt)
{
    std::memset(&msg, 0, sizeof(msg));
    auto *hdr = reinterpret_cast<CONSOLE_MSG_HEADER *>(msg.body);
    hdr->ApiNumber = static_cast<ULONG>(ConsolepAddAlias); // 0x12 L3-18
    hdr->ApiDescriptorSize = sizeof(CONSOLE_ADDALIAS_MSG);

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
    api_l3_add_alias(msg, st, nullptr, nullptr, nullptr);

    ASSERT(st.aliases.size() == 1);
    auto it = st.aliases.find(L"hello");
    ASSERT(it != st.aliases.end());
    ASSERT(it->second == L"echo hello");
    return true;
}

// 回归: ExeLength=0 (无 exe 名)
bool test_regression_add_alias_zero_exe()
{
    console_state st;
    miniio::io_msg msg;

    mock_add_alias_msg(msg, L"", L"x", L"exit");
    api_l3_add_alias(msg, st, nullptr, nullptr, nullptr);

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
    api_l3_add_alias(msg, st, nullptr, nullptr, nullptr);
    ASSERT(st.aliases.size() == 1);

    pipe_bridge bridge;
    bridge.cstate = &st;
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
    api_l3_add_alias(msg, st, nullptr, nullptr, nullptr);
    ASSERT(st.aliases.size() == 1);

    pipe_bridge bridge;
    bridge.cstate = &st;
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

    auto *alias = reinterpret_cast<CONSOLE_ADDALIAS_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    alias->SourceLength = 0; // zero-length→skip
    alias->TargetLength = 0;
    alias->ExeLength = 0;
    alias->Unicode = FALSE; // ANSI with zero lengths

    api_l3_add_alias(msg, st, nullptr, nullptr, nullptr);
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

    api_l3_get_alias(msg, st, nullptr, nullptr, nullptr);

    auto *r = reinterpret_cast<CONSOLE_GETALIAS_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    // TargetLength 应为字节数: "echo hello" = 10 wchars × 2 = 20
    ASSERT(r->TargetLength == 20);

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

    api_l3_get_alias(msg, st, nullptr, nullptr, nullptr);

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

    auto *r = reinterpret_cast<CONSOLE_GETALIASES_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    r->Unicode = TRUE;

    api_l3_get_aliases(msg, st, nullptr, nullptr, nullptr);

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

    auto *r = reinterpret_cast<CONSOLE_GETALIASESLENGTH_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    r->Unicode = FALSE;

    api_l3_get_aliases_length(msg, st, nullptr, nullptr, nullptr);

    // "x\0exit\0" = 1 + 1 + 4 + 1 = 7 字节
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

    console_state st;
    api_l3_get_history_length(msg, st, nullptr, nullptr, nullptr);

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

    console_state st;
    api_l3_get_history(msg, st, nullptr, nullptr, nullptr);

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

    auto *r = reinterpret_cast<CONSOLE_GETTITLE_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    r->Unicode = TRUE;
    r->Original = FALSE;

    api_get_title(msg, st, nullptr, nullptr, nullptr);

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
    api_l3_add_alias(msg, st, nullptr, nullptr, nullptr);

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
    auto *r = reinterpret_cast<CONSOLE_GETALIAS_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    r->SourceLength = static_cast<USHORT>(src_a.size());
    r->ExeLength = static_cast<USHORT>(exe_a.size());
    r->Unicode = FALSE;

    BYTE *data = msg.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETALIAS_MSG);
    std::memcpy(data, exe_a.data(), exe_a.size());
    data += exe_a.size();
    std::memcpy(data, src_a.data(), src_a.size());

    api_l3_get_alias(msg, st, nullptr, nullptr, nullptr);

    // TargetLength 应为 ANSI 字节数: "dir" = 3
    ASSERT(r->TargetLength == 3);
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

    auto *r = reinterpret_cast<CONSOLE_GETALIASES_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    r->Unicode = FALSE;

    api_l3_get_aliases(msg, st, nullptr, nullptr, nullptr);

    // 序列化: "x\0exit\0" = 1 + 1 + 4 + 1 = 7 字节
    ASSERT(r->AliasesBufferLength == 7);
    auto *out = reinterpret_cast<const char *>(msg.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETALIASES_MSG));
    ASSERT(out[0] == 'x' && out[1] == '\0');
    std::string exit_str{out + 2, 4};
    ASSERT(exit_str == "exit");
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

    auto *r = reinterpret_cast<CONSOLE_GETTITLE_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    r->Unicode = FALSE;
    r->Original = FALSE;

    api_get_title(msg, st, nullptr, nullptr, nullptr);

    // "cmd" = 3 字节
    ASSERT(r->TitleLength == 3);
    auto *out = reinterpret_cast<const char *>(msg.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETTITLE_MSG));
    std::string title_str{out, 3};
    ASSERT(title_str == "cmd");
    return true;
}

#include "conpty/pipe_bridge.hpp"

bool test_alias_expand_simple_match()
{
    console_state st;
    st.aliases[L"hello"] = L"echo hello";

    pipe_bridge bridge;
    bridge.cstate = &st;
    bridge.test_cooked_append(U"hello", 5);
    bridge.test_expand_alias();

    ASSERT(bridge.test_get_cooked_buf() == U"echo hello");
    return true;
}

bool test_alias_expand_with_trailing_args()
{
    console_state st;
    st.aliases[L"gs"] = L"git status";

    pipe_bridge bridge;
    bridge.cstate = &st;
    bridge.test_cooked_append(U"gs --short", 10);
    bridge.test_expand_alias();

    ASSERT(bridge.test_get_cooked_buf() == U"git status --short");
    return true;
}

bool test_alias_expand_no_match_passthrough()
{
    console_state st;
    st.aliases[L"hello"] = L"echo hello";

    pipe_bridge bridge;
    bridge.cstate = &st;
    bridge.test_cooked_append(U"world", 5);
    bridge.test_expand_alias();

    ASSERT(bridge.test_get_cooked_buf() == U"world");
    return true;
}

bool test_alias_expand_empty_aliases()
{
    console_state st;

    pipe_bridge bridge;
    bridge.cstate = &st;
    bridge.test_cooked_append(U"anything", 8);
    bridge.test_expand_alias();

    ASSERT(bridge.test_get_cooked_buf() == U"anything");
    return true;
}

bool test_alias_expand_single_word()
{
    console_state st;
    st.aliases[L"x"] = L"exit";

    pipe_bridge bridge;
    bridge.cstate = &st;
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
    // 测试 DEC 特殊图形字符映射
    // 'j' (0x6A) → U+2518 (┘)
    ASSERT(console_state::dec_to_unicode(0x6A) == 0x2518);
    // 'k' (0x6B) → U+2510 (┐)
    ASSERT(console_state::dec_to_unicode(0x6B) == 0x2510);
    // 'l' (0x6C) → U+250C (┌)
    ASSERT(console_state::dec_to_unicode(0x6C) == 0x250C);
    // 'm' (0x6D) → U+2514 (└)
    ASSERT(console_state::dec_to_unicode(0x6D) == 0x2514);
    // 'n' (0x6E) → U+253C (┼)
    ASSERT(console_state::dec_to_unicode(0x6E) == 0x253C);
    // 'x' (0x78) → U+2502 (│)
    ASSERT(console_state::dec_to_unicode(0x78) == 0x2502);
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

    RUN_TEST(test_history_default, L"History default");
    RUN_TEST(test_history_add, L"History add");
    RUN_TEST(test_history_clear, L"History clear");

    RUN_TEST(test_alias_add_and_find, L"Alias add/find");
    RUN_TEST(test_alias_empty, L"Alias empty");
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
