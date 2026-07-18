# ═══════════════════════════════════════════════════════════════
# corehost — CoreHost可执行文件
# ═══════════════════════════════════════════════════════════════

add_executable(corehost WIN32
    corehost.rc
    corehost.manifest
    cli/main.cpp
    defterm/defterm.cpp
    defterm/signal.cpp
    comserver/com_server.cpp
)

target_sources(corehost
    PRIVATE
        FILE_SET HEADERS
            FILES
                cli/console_arguments.hpp
                client/client.hpp
                comserver/com_server.hpp
                defterm/connect_policy.hpp
                defterm/defterm.hpp
                defterm/io.hpp
                defterm/signal.hpp
                defterm/terminal_handoff.hpp
)

target_include_directories(corehost PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(corehost PRIVATE
    libcorehost
    conhost_proxy_headers
)

corehost_target_msvc_linker_option(corehost PRIVATE "/SUBSYSTEM:WINDOWS,10.0")
