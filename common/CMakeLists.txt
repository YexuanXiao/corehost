# ═══════════════════════════════════════════════════════════════
# common — 核心基础设施静态库
#
# 提供 ConDrv I/O 原语 (miniio), OS 头文件 (os),
# NT API 封装 (ntapi), Win32 RAII (win32),
# COM 工具 (com), 工具函数 (utility), Shell 检测 (shell).
# ═══════════════════════════════════════════════════════════════

add_library(common STATIC
    ntapi/consolecontrol.cpp
    ntapi/consolenslmode.cpp
    utility/log.cpp
    utility/notification.cpp
)

target_include_directories(common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/third_parties/cstring_view/include/beman
    ${CMAKE_SOURCE_DIR}/third_parties/utf_view/include/beman
    ${CMAKE_SOURCE_DIR}/proxy/${CONHOST_PROXY_ARCH_DIR}
)

target_link_libraries(common PUBLIC
    ntdll.lib
    ole32.lib
    psapi.lib
    runtimeobject.lib
    shell32.lib
)
