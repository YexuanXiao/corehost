# ═══════════════════════════════════════════════════════════════
# libcorehost — ConPTY 核心静态库
#
# 提供 conpty_entry() 函数作为外部接口。
# ═══════════════════════════════════════════════════════════════

add_library(libcorehost STATIC
    conpty/conpty.cpp
    conpty/signal.cpp
    ntapi/consolecontrol.cpp
    ntapi/consolenslmode.cpp
)

target_include_directories(libcorehost PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/third_parties/cstring_view/include/beman
    ${CMAKE_SOURCE_DIR}/third_parties/utf_view/include/beman
    ${CMAKE_SOURCE_DIR}/third_parties/deque
    ${CMAKE_SOURCE_DIR}/proxy/${CONHOST_PROXY_ARCH_DIR}
)

target_link_libraries(libcorehost PUBLIC
    unicode::unicode
)
