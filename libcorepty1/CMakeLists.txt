# ═══════════════════════════════════════════════════════════════
# libcorepty1 — ConPTY 核心静态库
#
# 提供 conpty_entry() 函数作为外部接口。
# ═══════════════════════════════════════════════════════════════

add_library(libcorepty1 STATIC
    conpty.cpp
    signal.cpp
)

target_sources(libcorepty1
    PUBLIC
        FILE_SET HEADERS
            FILES
                api_handlers.hpp
                api_router.hpp
                char_convert.hpp
                char_width.hpp
                command_history_state.hpp
                vt_input_state.hpp
                condrv_io.hpp
                console_ctrl.hpp
                console_state.hpp
                console_viewport.hpp
                conpty.hpp
                conversion_buffers.hpp
                default_console_size.hpp
                gbk_table.hpp
                input_buffer.hpp
                io_state.hpp
                io_threads.hpp
                message_router.hpp
                pending_io_state.hpp
                perf_diag.hpp
                pipe_bridge.hpp
                pipe_bridge_io.hpp
                pseudo_window.hpp
                raw_buffers.hpp
                screen_buffer.hpp
                screen_buffer_row.hpp
                signal.hpp
                terminal_cursor_state.hpp
                text_measurement_mode.hpp
                viewport_render.hpp
                input_engine.hpp
                vt_msg_dispatch.hpp
                output_buffer.hpp
                vt_parser.hpp
)

target_include_directories(libcorepty1 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_SOURCE_DIR}/third_parties/deque
)

target_link_libraries(libcorepty1 PUBLIC
    common
    unicode::unicode
)

if(COREHOST_DEFAULT_UTF8)
    target_compile_definitions(libcorepty1 PRIVATE COREHOST_DEFAULT_UTF8)
endif()

if(COREHOST_RUN_AS_UTF8)
    target_compile_definitions(libcorepty1 PRIVATE COREHOST_RUN_AS_UTF8)
endif()