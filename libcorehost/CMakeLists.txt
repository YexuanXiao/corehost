# ═══════════════════════════════════════════════════════════════
# libcorehost — ConPTY 核心静态库
#
# 提供 conpty_entry() 函数作为外部接口。
# ═══════════════════════════════════════════════════════════════

add_library(libcorehost STATIC
    conpty.cpp
    signal.cpp
)

target_sources(libcorehost
    PUBLIC
        FILE_SET HEADERS
            FILES
                api_handlers.hpp
                api_router.hpp
                char_convert.hpp
                char_width.hpp
                command_history_state.hpp
                condrv_io.hpp
                connect_completion.hpp
                console_state.hpp
                console_viewport.hpp
                conpty.hpp
                conversion_buffers.hpp
                default_console_size.hpp
                gbk_table.hpp
                input_buffer.hpp
                io_loop.hpp
                io_state.hpp
                message_router.hpp
                pending_io_state.hpp
                perf_diag.hpp
                pipe_bridge.hpp
                pipe_bridge_io.hpp
                process_list_snapshot.hpp
                raw_byte_allocator.hpp
                screen_buffer.hpp
                screen_buffer_row.hpp
                signal.hpp
                terminal_cursor_state.hpp
                text_measurement_mode.hpp
                viewport_render.hpp
                vt_input_engine.hpp
                vt_msg_dispatch.hpp
                vt_output_buffer.hpp
                vt_parser.hpp
)

target_include_directories(libcorehost PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/third_parties/deque
)

target_link_libraries(libcorehost PUBLIC
    common
    unicode::unicode
)

if(COREHOST_USE_SYSTEM_ICU)
    target_compile_definitions(libcorehost PUBLIC COREHOST_USE_SYSTEM_ICU)
    target_link_libraries(libcorehost PUBLIC icu)
endif()
