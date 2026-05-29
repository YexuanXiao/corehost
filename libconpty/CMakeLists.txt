# ═══════════════════════════════════════════════════════════════
# libconpty — ConPTY 静态库 + DLL
#
# conpty_static (STATIC)
# conpty        (SHARED)
# ═══════════════════════════════════════════════════════════════

option(USE_INBOX_CONHOST "Always use system conhost.exe (no corehost side-load)" OFF)

# ── conpty_static: 静态库，继承父级所有编译选项 ──────────
add_library(conpty_static STATIC
    libconpty.cpp
)

target_include_directories(conpty_static PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../libcorehost
)

target_link_libraries(conpty_static PUBLIC
    common
)

if(USE_INBOX_CONHOST)
    target_compile_definitions(conpty_static PRIVATE USE_INBOX_CONHOST)
endif()

# ── conpty (DLL): 独立编译选项 ────
add_library(conpty SHARED
    libconpty.cpp
    dllmain.cpp
    conpty.def
)

target_include_directories(conpty PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../common
    ${CMAKE_CURRENT_SOURCE_DIR}/../libcorehost
    ${CMAKE_CURRENT_SOURCE_DIR}/../third_parties/cstring_view/include/beman
)

target_link_libraries(conpty PUBLIC
    shell32.lib
    ole32.lib
    ntdll.lib
)

# DLL 特有的严格编译选项
target_compile_features(conpty PRIVATE cxx_std_20)

target_compile_definitions(conpty PRIVATE
    NOMINMAX
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    _HAS_EXCEPTIONS=0
    CONPTY_NO_CPP_STDLIB
)

if(USE_INBOX_CONHOST)
    target_compile_definitions(conpty PRIVATE USE_INBOX_CONHOST)
endif()

if(MSVC)
    target_compile_options(conpty PRIVATE
        /utf-8
        /W4
        /permissive-
        /Zc:preprocessor
        /Zc:__cplusplus
        /EHs-c-
        /Zc:throwingNew-
    )
endif()

target_link_options(conpty PRIVATE
    /NODEFAULTLIB:libcpmt
    /NODEFAULTLIB:msvcprt
)
