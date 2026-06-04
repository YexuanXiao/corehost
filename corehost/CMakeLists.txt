# ═══════════════════════════════════════════════════════════════
# corehost — CoreHost可执行文件
# ═══════════════════════════════════════════════════════════════

add_executable(corehost WIN32
    corehost.rc
    cli/main.cpp
    defterm/defterm.cpp
    defterm/signal.cpp
    comserver/com_server.cpp
)

target_include_directories(corehost PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(corehost PRIVATE
    libcorehost
    conhost_proxy
    conhost_proxy_headers
)

if(MSVC)
    target_link_options(corehost PRIVATE
        "/MANIFEST:EMBED"
        "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/corehost.manifest"
        "/SUBSYSTEM:WINDOWS,10.0"
    )
endif()
