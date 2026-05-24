# CMakeLists.txt for tests CTest 管理
cmake_minimum_required(VERSION 3.30)

# ── 测试选项 ──────────────────────────────────────────
option(BUILD_TESTS "Build and register all tests with CTest" ON)

if(NOT BUILD_TESTS)
    return()
endif()

enable_testing()

# ── 辅助测试(公共头文 ───────────────────────────
add_library(test_common INTERFACE)
target_include_directories(test_common INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/libcorehost
    ${CMAKE_SOURCE_DIR}/corehost
    ${CMAKE_SOURCE_DIR}/third_parties/cstring_view/include/beman
    ${CMAKE_SOURCE_DIR}/third_parties/libunicode/src
    ${CMAKE_SOURCE_DIR}/proxy/${CONHOST_PROXY_ARCH_DIR}
)
target_link_libraries(test_common INTERFACE shell32.lib ole32.lib ntdll.lib)
target_compile_features(test_common INTERFACE cxx_std_23)

if(MSVC)
    target_compile_options(test_common INTERFACE /utf-8)
endif()

# ── test_vt_parser ──────────────────────────────────
# add_executable(test_vt_parser
#     test_vt_parser.cpp
# )
# target_link_libraries(test_vt_parser PRIVATE test_common)
# target_include_directories(test_vt_parser PRIVATE
#     ${CMAKE_SOURCE_DIR}/libcorehost
#     ${CMAKE_SOURCE_DIR}/third_parties/cstring_view/include/beman
# )
# add_test(NAME VT.Parser COMMAND test_vt_parser)
# 
# set_tests_properties(VT.Parser PROPERTIES
#     LABELS "vt"
#     TIMEOUT 30
# )

# ── test_vt_parser_utf32 ───────────────────────────
add_executable(test_vt_parser_utf32
    test_vt_parser_utf32.cpp
)
target_link_libraries(test_vt_parser_utf32 PRIVATE test_common)
target_include_directories(test_vt_parser_utf32 PRIVATE
    ${CMAKE_SOURCE_DIR}/libcorehost
    ${CMAKE_SOURCE_DIR}/third_parties/cstring_view/include/beman
)
add_test(NAME VT.Parser.Utf32 COMMAND test_vt_parser_utf32)

set_tests_properties(VT.Parser.Utf32 PROPERTIES
    LABELS "vt"
    TIMEOUT 30
)

# ── test_char_convert ──────────────────────────────
add_executable(test_char_convert
    test_char_convert.cpp
)
target_link_libraries(test_char_convert PRIVATE test_common unicode::unicode)
target_include_directories(test_char_convert PRIVATE
    ${CMAKE_SOURCE_DIR}/libcorehost
    ${CMAKE_SOURCE_DIR}/third_parties/cstring_view/include/beman
)
add_test(NAME Convert.Char COMMAND test_char_convert)
set_tests_properties(Convert.Char PROPERTIES LABELS "convert" TIMEOUT 30)

# ── test_screen_buffer ────────────────────────────
add_executable(test_screen_buffer
    test_screen_buffer.cpp
)
target_link_libraries(test_screen_buffer PRIVATE test_common unicode::unicode)
add_test(NAME ScreenBuffer COMMAND test_screen_buffer)
set_tests_properties(ScreenBuffer PROPERTIES LABELS "sb" TIMEOUT 30)

# ── test_console_state ────────────────────────────
add_executable(test_console_state
    test_console_state.cpp
)
target_link_libraries(test_console_state PRIVATE test_common unicode::unicode)
target_include_directories(test_console_state PRIVATE
    ${CMAKE_SOURCE_DIR}/libcorehost
    ${CMAKE_SOURCE_DIR}/third_parties/cstring_view/include/beman
)
add_test(NAME ConsoleState COMMAND test_console_state)
set_tests_properties(ConsoleState PROPERTIES LABELS "state" TIMEOUT 30)

# ── test_vt_msg_dispatch ──────────────────────────
add_executable(test_vt_msg_dispatch
    test_vt_msg_dispatch.cpp
)
target_link_libraries(test_vt_msg_dispatch PRIVATE test_common unicode::unicode)
add_test(NAME VtMsgDispatch COMMAND test_vt_msg_dispatch)
set_tests_properties(VtMsgDispatch PROPERTIES LABELS "vtmsg" TIMEOUT 30)

# ── test_conpty_newline ────────────────────────────
add_executable(test_conpty_newline
    test_conpty_newline.cpp
)
target_link_libraries(test_conpty_newline PRIVATE test_common unicode::unicode)
add_test(NAME ConPTY.Newline COMMAND test_conpty_newline)
set_tests_properties(ConPTY.Newline PROPERTIES LABELS "conpty;newline" TIMEOUT 30)

# ── test_conpty_keyboard ───────────────────────────
add_executable(test_conpty_keyboard
    test_conpty_keyboard.cpp
)
target_link_libraries(test_conpty_keyboard PRIVATE test_common unicode::unicode)
add_test(NAME ConPTY.Keyboard COMMAND test_conpty_keyboard)
set_tests_properties(ConPTY.Keyboard PROPERTIES LABELS "conpty;keyboard" TIMEOUT 30)

# ── test_conpty_cursor_sync ─────────────────────────
add_executable(test_conpty_cursor_sync
    test_conpty_cursor_sync.cpp
)
target_link_libraries(test_conpty_cursor_sync PRIVATE test_common unicode::unicode)
target_include_directories(test_conpty_cursor_sync PRIVATE
    ${CMAKE_SOURCE_DIR}/libcorehost
    ${CMAKE_SOURCE_DIR}/third_parties/cstring_view/include/beman
)
add_test(NAME ConPTY.CursorSync COMMAND test_conpty_cursor_sync)
set_tests_properties(ConPTY.CursorSync PROPERTIES LABELS "conpty;cursor" TIMEOUT 30)
# pipe_bridge.hpp utility/log.hpp 静态初始化需要在 exe 目录下有 logs/ 子目
add_custom_command(TARGET test_conpty_cursor_sync POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:test_conpty_cursor_sync>/logs"
)

# ── test_conpty_e2e ───────────────────────────────
add_executable(test_conpty_e2e
    test_conpty_e2e.cpp
)
target_include_directories(test_conpty_e2e PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/libcorehost
    ${CMAKE_SOURCE_DIR}/third_parties/cstring_view/include/beman
)
target_link_libraries(test_conpty_e2e PRIVATE test_common unicode::unicode conpty_static)
add_dependencies(test_conpty_e2e corehost)
add_custom_command(TARGET test_conpty_e2e POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:corehost>"
        "$<TARGET_FILE_DIR:test_conpty_e2e>"
)
add_test(NAME ConPTY.E2E COMMAND test_conpty_e2e)
set_tests_properties(ConPTY.E2E PROPERTIES LABELS "conpty;e2e" TIMEOUT 60)

# ── test_commandline ───────────────────────────────
add_executable(test_commandline
    test_commandline.cpp
)
target_link_libraries(test_commandline PRIVATE test_common)
target_include_directories(test_commandline PRIVATE
    ${CMAKE_SOURCE_DIR}/libcorehost
    ${CMAKE_SOURCE_DIR}/third_parties/cstring_view/include/beman
)
add_test(NAME CLI.CommandLine COMMAND test_commandline)

set_tests_properties(CLI.CommandLine PROPERTIES
    LABELS "cli"
    TIMEOUT 60
)

# ══════════════════════════════════════════════════════
# 手动验证程序 (manual) 每个测试独立 .exe, 不注CTest
# 用法: ConPTY (corehost.exe) 下运 肉眼观察输出
# ══════════════════════════════════════════════════════

# ── 公共头文 manual/manual_common.hpp ──
# 所manual 目标自动包含 manual/ 目录
add_library(manual_common INTERFACE)
target_include_directories(manual_common INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/manual)
target_compile_features(manual_common INTERFACE cxx_std_17)
if(MSVC)
    target_compile_options(manual_common INTERFACE /utf-8)
endif()

# ──  add_manual_test(name source) ──
macro(add_manual_test name src)
    add_executable(${name} manual/${src})
    target_link_libraries(${name} PRIVATE manual_common)
    set_target_properties(${name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/tests/Release/manual"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_BINARY_DIR}/tests/Debug/manual"
    )
endmacro()

# ══════════════API 测试 (api_001 ~ api_015) ══════════
add_manual_test(api_001_write_console          api_001_write_console.cpp)
add_manual_test(api_002_screen_buffer_info     api_002_screen_buffer_info.cpp)
add_manual_test(api_003_cursor_position        api_003_cursor_position.cpp)
add_manual_test(api_004_fill_character         api_004_fill_character.cpp)
add_manual_test(api_005_fill_attribute         api_005_fill_attribute.cpp)
add_manual_test(api_006_write_output_char      api_006_write_output_char.cpp)
add_manual_test(api_007_write_output_attr      api_007_write_output_attr.cpp)
add_manual_test(api_008_scroll                 api_008_scroll.cpp)
add_manual_test(api_009_text_attr              api_009_text_attr.cpp)
add_manual_test(api_010_cursor_info            api_010_cursor_info.cpp)
add_manual_test(api_011_title                  api_011_title.cpp)
add_manual_test(api_012_console_mode           api_012_console_mode.cpp)
add_manual_test(api_013_codepage               api_013_codepage.cpp)
add_manual_test(api_014_largest_window         api_014_largest_window.cpp)
add_manual_test(api_015_read_console           api_015_read_console.cpp)

# ══════════════VT 测试 (vt_001 ~ vt_018) ════════════
add_manual_test(vt_001_cursor                  vt_001_cursor.cpp)
add_manual_test(vt_002_sgr_16color             vt_002_sgr_16color.cpp)
add_manual_test(vt_003_sgr_extended            vt_003_sgr_extended.cpp)
add_manual_test(vt_004_sgr_attrs               vt_004_sgr_attrs.cpp)
add_manual_test(vt_005_ed                      vt_005_ed.cpp)
add_manual_test(vt_006_el                      vt_006_el.cpp)
add_manual_test(vt_007_decsc_decrc             vt_007_decsc_decrc.cpp)
add_manual_test(vt_008_cursor_style            vt_008_cursor_style.cpp)
add_manual_test(vt_009_su_sd_il_dl             vt_009_su_sd_il_dl.cpp)
add_manual_test(vt_010_osc                     vt_010_osc.cpp)
add_manual_test(vt_011_rep_dch_ich             vt_011_rep_dch_ich.cpp)
add_manual_test(vt_012_da_dsr                  vt_012_da_dsr.cpp)
add_manual_test(vt_013_decawm                  vt_013_decawm.cpp)
add_manual_test(vt_014_charset                 vt_014_charset.cpp)
add_manual_test(vt_015_alt_buffer              vt_015_alt_buffer.cpp)
add_manual_test(vt_016_ris                     vt_016_ris.cpp)
add_manual_test(vt_017_resize                  vt_017_resize.cpp)
add_manual_test(vt_018_comprehensive           vt_018_comprehensive.cpp)

# ══════════════Unicode 测试 (uni_001 ~ uni_018) ══════
add_manual_test(uni_001_bmp                    uni_001_bmp.cpp)
add_manual_test(uni_002_combining              uni_002_combining.cpp)
add_manual_test(uni_003_emoji_basic            uni_003_emoji_basic.cpp)
add_manual_test(uni_004_emoji_flags            uni_004_emoji_flags.cpp)
add_manual_test(uni_005_rtl                    uni_005_rtl.cpp)
add_manual_test(uni_006_cjk                    uni_006_cjk.cpp)
add_manual_test(uni_007_zero_width             uni_007_zero_width.cpp)
add_manual_test(uni_008_tab_unicode            uni_008_tab_unicode.cpp)
add_manual_test(uni_009_math                   uni_009_math.cpp)
add_manual_test(uni_010_vs                     uni_010_vs.cpp)
add_manual_test(uni_011_line_break             uni_011_line_break.cpp)
add_manual_test(uni_012_control_pictures       uni_012_control_pictures.cpp)
add_manual_test(uni_013_spaces                 uni_013_spaces.cpp)
add_manual_test(uni_014_ancient                uni_014_ancient.cpp)
add_manual_test(uni_015_supplementary          uni_015_supplementary.cpp)
add_manual_test(uni_016_ligatures              uni_016_ligatures.cpp)
add_manual_test(uni_017_mixed_emoji_text       uni_017_mixed_emoji_text.cpp)
add_manual_test(uni_018_long_text_wrap         uni_018_long_text_wrap.cpp)
