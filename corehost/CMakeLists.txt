# ═══════════════════════════════════════════════════════════════
# corehost / corehost2 — CoreHost 可执行文件
# ═══════════════════════════════════════════════════════════════

# libcorepty1 的 COM handoff 适配层。单独编译，避免 libcorepty1 与
# libcorepty2 的同名头文件互相遮蔽。
add_library(corehost1_support STATIC
    support/com_handoff_bridge.cpp
    comserver/com_server.cpp
)

target_include_directories(corehost1_support PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}
)

target_link_libraries(corehost1_support PRIVATE
    libcorepty1
    conhost_proxy_headers
)

# 原始 corehost.exe：完全使用 libcorepty1 的实现。
add_executable(corehost WIN32
    corehost.rc
    $<IF:$<OR:$<BOOL:${COREHOST_DEFAULT_UTF8}>,$<BOOL:${COREHOST_RUN_AS_UTF8}>>,corehost.utf8.manifest,corehost.manifest>
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
                defterm/handoff_session.hpp
                defterm/terminal_handoff.hpp
)

target_include_directories(corehost PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(corehost PRIVATE
    libcorepty1
    conhost_proxy_headers
)

corehost_target_msvc_linker_options(corehost PRIVATE "/OPT:REF" "/OPT:ICF")
corehost_target_msvc_linker_option(corehost PRIVATE "/SUBSYSTEM:WINDOWS,10.0")

# corehost2.exe：ConPTY 会话入口使用 libcorepty2。
add_executable(corehost2 WIN32
    corehost.rc
    $<IF:$<OR:$<BOOL:${COREHOST_DEFAULT_UTF8}>,$<BOOL:${COREHOST_RUN_AS_UTF8}>>,corehost.utf8.manifest,corehost.manifest>
    cli/corehost2_main.cpp
)

target_sources(corehost2
    PRIVATE
        FILE_SET HEADERS
            FILES
                support/com_handoff_bridge.hpp
)

target_include_directories(corehost2 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_BINARY_DIR}/libcorepty1
)

target_link_libraries(corehost2 PRIVATE
    libcorepty2
    "$<LINK_ONLY:corehost1_support>"
    "$<LINK_ONLY:libcorepty1>"
    shell32
    conhost_proxy_headers
)

corehost_target_msvc_linker_options(corehost2 PRIVATE "/OPT:REF" "/OPT:ICF")
corehost_target_msvc_linker_option(corehost2 PRIVATE "/SUBSYSTEM:WINDOWS,10.0")
