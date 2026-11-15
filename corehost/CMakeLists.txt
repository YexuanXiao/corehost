# ═══════════════════════════════════════════════════════════════
# corehost — CoreHost 可执行文件
# ═══════════════════════════════════════════════════════════════

add_library(corehost1_support STATIC
    support/com_handoff_bridge.cpp
    defterm/defterm.cpp
    defterm/signal.cpp
    comserver/com_server.cpp
)

target_include_directories(corehost1_support PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}
)

target_link_libraries(corehost1_support PRIVATE
    libcorehost
    conhost_proxy_headers
)

add_executable(corehost WIN32
    corehost.rc
    $<IF:$<OR:$<BOOL:${COREHOST_DEFAULT_UTF8}>,$<BOOL:${COREHOST_RUN_AS_UTF8}>>,corehost.utf8.manifest,corehost.manifest>
    cli/corehost2_main.cpp
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
                defterm/handoff_session.hpp
                defterm/terminal_handoff.hpp
)

target_include_directories(corehost PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_BINARY_DIR}/libcorehost
)

target_link_libraries(corehost PRIVATE
    libcorehost2
    "$<LINK_ONLY:corehost1_support>"
    "$<LINK_ONLY:libcorehost>"
    shell32
    conhost_proxy_headers
)

corehost_target_msvc_linker_option(corehost PRIVATE "/SUBSYSTEM:WINDOWS,10.0")
