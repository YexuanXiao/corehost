# ═══════════════════════════════════════════════════════════════
# corehost — CoreHost executable
# ═══════════════════════════════════════════════════════════════

configure_file(version.h.in ${CMAKE_CURRENT_BINARY_DIR}/version.h @ONLY)

add_executable(corehost WIN32
    corehost.rc
    $<IF:$<OR:$<BOOL:${COREHOST_DEFAULT_UTF8}>,$<BOOL:${COREHOST_RUN_AS_UTF8}>>,corehost.utf8.manifest,corehost.manifest>
    cli/main.cpp
    comserver/com_server.cpp
    support/com_handoff_bridge.cpp
)

target_sources(corehost
    PRIVATE
        FILE_SET HEADERS
            FILES
                cli/console_arguments.hpp
                client/client.hpp
                comserver/com_server.hpp
                support/com_handoff_bridge.hpp
)

target_include_directories(corehost PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(corehost PRIVATE
    defterm
    libcorepty
    shell32
    conhost_proxy_headers
)

corehost_target_msvc_linker_options(corehost PRIVATE "/OPT:REF" "/OPT:ICF")
corehost_target_msvc_linker_option(corehost PRIVATE "/SUBSYSTEM:WINDOWS,10.0")
