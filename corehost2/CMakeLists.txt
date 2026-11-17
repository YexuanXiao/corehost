# ═══════════════════════════════════════════════════════════════
# corehost2 — CoreHost executable
# ═══════════════════════════════════════════════════════════════

configure_file(version.h.in ${CMAKE_CURRENT_BINARY_DIR}/version.h @ONLY)

add_executable(corehost2 WIN32
    corehost2.rc
    $<IF:$<OR:$<BOOL:${COREHOST_DEFAULT_UTF8}>,$<BOOL:${COREHOST_RUN_AS_UTF8}>>,corehost2.utf8.manifest,corehost2.manifest>
    cli/main.cpp
    comserver/com_server.cpp
    support/com_handoff_bridge.cpp
)

target_sources(corehost2
    PRIVATE
        FILE_SET HEADERS
            FILES
                cli/console_arguments.hpp
                client/client.hpp
                comserver/com_server.hpp
                support/com_handoff_bridge.hpp
)

target_include_directories(corehost2 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(corehost2 PRIVATE
    defterm2
    libcorepty2
    shell32
    conhost_proxy_headers
)

corehost_target_msvc_linker_options(corehost2 PRIVATE "/OPT:REF" "/OPT:ICF")
corehost_target_msvc_linker_option(corehost2 PRIVATE "/SUBSYSTEM:WINDOWS,10.0")
