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
    ${CMAKE_SOURCE_DIR}/third_parties/libunicode/src
    ${CMAKE_SOURCE_DIR}/third_parties/deque
)

target_link_libraries(test_common INTERFACE common)
target_compile_features(test_common INTERFACE cxx_std_23)

if(MSVC)
    target_compile_options(test_common INTERFACE /utf-8)
endif()

# ── test_vt_parser_utf32 ───────────────────────────
add_executable(test_vt_parser_utf32
    test_vt_parser_utf32.cpp
)
target_link_libraries(test_vt_parser_utf32 PRIVATE test_common)
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
add_test(NAME Convert.Char COMMAND test_char_convert)
set_tests_properties(Convert.Char PROPERTIES LABELS "convert" TIMEOUT 30)

# ── test_raw_byte_allocator ────────────────────────
add_executable(test_raw_byte_allocator
    test_raw_byte_allocator.cpp
)
target_link_libraries(test_raw_byte_allocator PRIVATE test_common)
add_test(NAME Utility.RawByteAllocator COMMAND test_raw_byte_allocator)
set_tests_properties(Utility.RawByteAllocator PROPERTIES LABELS "utility" TIMEOUT 30)

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

# ── test_conpty_writeconsole_cursor ──────────────────
add_executable(test_conpty_writeconsole_cursor
    test_conpty_writeconsole_cursor.cpp
)
target_link_libraries(test_conpty_writeconsole_cursor PRIVATE test_common unicode::unicode)
add_test(NAME ConPTY.WriteConsoleCursor COMMAND test_conpty_writeconsole_cursor)
set_tests_properties(ConPTY.WriteConsoleCursor PROPERTIES
    LABELS "conpty;writeconsole;cursor"
    TIMEOUT 30
)

# ── test_commandline ───────────────────────────────
add_executable(test_commandline
    test_commandline.cpp
)
target_link_libraries(test_commandline PRIVATE test_common)
add_test(NAME CLI.CommandLine COMMAND test_commandline)

set_tests_properties(CLI.CommandLine PROPERTIES
    LABELS "cli"
    TIMEOUT 60
)

# ══════════════════════════════════════════════════════
# 手动验证程序 (manual) — 独立 CMakeLists.txt
# ══════════════════════════════════════════════════════
add_subdirectory(manual)
