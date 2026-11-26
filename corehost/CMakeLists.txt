# ═══════════════════════════════════════════════════════════════
# corehost — CoreHost executable
# ═══════════════════════════════════════════════════════════════

configure_file(version.h.in ${CMAKE_CURRENT_BINARY_DIR}/version.h @ONLY)

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
                defterm/connect_policy.hpp
                defterm/console_session.hpp
                defterm/defterm.hpp
                defterm/handoff_session.hpp
                defterm/signal.hpp
                defterm/terminal_handoff.hpp
                client/client.hpp
                comserver/com_server.hpp
)

target_include_directories(corehost PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/cli
    ${CMAKE_CURRENT_SOURCE_DIR}/defterm
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(corehost PRIVATE
    libconsole
    libcoreio
    libcorepty
    libinterop
    libpseudowindow
    libscreen
    libvtparser
    libvtstate
    shell32
    conhost_proxy_headers
    ole32
    user32
)

corehost_target_msvc_linker_options(corehost PRIVATE "/OPT:REF" "/OPT:ICF")
corehost_target_msvc_linker_option(corehost PRIVATE "/SUBSYSTEM:WINDOWS,10.0")

add_executable(corehostw WIN32
    corehostw.rc
    $<IF:$<OR:$<BOOL:${COREHOST_DEFAULT_UTF8}>,$<BOOL:${COREHOST_RUN_AS_UTF8}>>,corehost.utf8.manifest,corehost.manifest>
    cli/main.cpp
    defterm/defterm.cpp
    defterm/signal.cpp
    comserver/com_server.cpp
    window/window_client_process.cpp
    window/window_console_session.cpp
    window/window_entry.cpp
)

target_compile_definitions(corehostw PRIVATE COREHOST_WINDOW=1)
add_dependencies(corehostw libd3d12ui_shaders)

target_sources(corehostw
    PRIVATE
        FILE_SET HEADERS
            FILES
                cli/console_arguments.hpp
                window/window_client_process.hpp
                window/window_console_session.hpp
                window/window_entry.hpp
)

target_include_directories(corehostw PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/cli
    ${CMAKE_CURRENT_SOURCE_DIR}/window
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(corehostw PRIVATE
    libconsole
    libcoreio
    libcorepty
    libcorewindow
    libinterop
    libpseudowindow
    libscreen
    libvtparser
    libvtstate
    conhost_proxy_headers
    common
    ole32
    shell32
    user32
)

corehost_target_msvc_linker_options(corehostw PRIVATE "/OPT:REF" "/OPT:ICF")
corehost_target_msvc_linker_option(corehostw PRIVATE "/SUBSYSTEM:WINDOWS,10.0")

