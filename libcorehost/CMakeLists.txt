# ═══════════════════════════════════════════════════════════════
# libcorehost — ConPTY 核心静态库
#
# 提供 conpty_entry() 函数作为外部接口。
# ═══════════════════════════════════════════════════════════════

add_library(libcorehost STATIC
    conpty.cpp
    signal.cpp
)

target_include_directories(libcorehost PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/third_parties/deque
)

target_link_libraries(libcorehost PUBLIC
    common
    unicode::unicode
)
