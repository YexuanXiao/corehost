# ═══════════════════════════════════════════════════════════════
# corehost — CoreHost可执行文件
# ═══════════════════════════════════════════════════════════════

add_executable(corehost WIN32
    cli/main.cpp
    defterm/defterm.cpp
    comserver/com_server.cpp
    ../libcorehost/shell/shell.cpp
)

target_include_directories(corehost PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/libcorehost
    ${CMAKE_SOURCE_DIR}/third_parties/cstring_view/include/beman
    ${CMAKE_SOURCE_DIR}/third_parties/utf_view/include/beman
    ${CMAKE_SOURCE_DIR}/third_parties/deque
    ${CMAKE_SOURCE_DIR}/proxy/${CONHOST_PROXY_ARCH_DIR}
)

target_link_libraries(corehost PRIVATE
    libcorehost
    ntdll.lib
    conhost_proxy
    conhost_proxy_headers
)
