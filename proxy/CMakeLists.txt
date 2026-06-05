set(conhost_PROXY_ARCH_DIR "${CONHOST_PROXY_ARCH_DIR}")

add_library(conhost_proxy_headers INTERFACE)
target_sources(conhost_proxy_headers
    INTERFACE FILE_SET HEADERS FILES
        ${conhost_PROXY_ARCH_DIR}/IConsoleHandoff.h
        ${conhost_PROXY_ARCH_DIR}/ITerminalHandoff.h
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/${conhost_PROXY_ARCH_DIR}
)

add_library(conhost_proxy SHARED
    ${conhost_PROXY_ARCH_DIR}/IConsoleHandoff_p.c
    ${conhost_PROXY_ARCH_DIR}/IConsoleHandoff_i.c
    ${conhost_PROXY_ARCH_DIR}/ITerminalHandoff_p.c
    ${conhost_PROXY_ARCH_DIR}/ITerminalHandoff_i.c
    ${conhost_PROXY_ARCH_DIR}/dlldata.c
)

# These files are MIDL-generated C sources but are compiled as C++ to keep the
# overall project C++-only and avoid a dedicated C toolchain configuration.
set_source_files_properties(
    ${conhost_PROXY_ARCH_DIR}/IConsoleHandoff_p.c
    ${conhost_PROXY_ARCH_DIR}/IConsoleHandoff_i.c
    ${conhost_PROXY_ARCH_DIR}/ITerminalHandoff_p.c
    ${conhost_PROXY_ARCH_DIR}/ITerminalHandoff_i.c
    ${conhost_PROXY_ARCH_DIR}/dlldata.c
    PROPERTIES
        LANGUAGE CXX
)

target_compile_definitions(conhost_proxy PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _WIN32_WINNT=0x0A00
    CINTERFACE
    COBJMACROS
    REGISTER_PROXY_DLL
    PROXY_CLSID_IS={0xDEC4804D,0x56D1,0x4F73,{0x9F,0xBE,0x68,0x28,0xE7,0xC8,0x5C,0x56}}
)

target_link_libraries(conhost_proxy PRIVATE
    rpcrt4
    ole32
    oleaut32
    conhost_proxy_headers
)

set_target_properties(conhost_proxy PROPERTIES
    OUTPUT_NAME OpenConsoleProxy
    LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/conhost_proxy.def
)

# The .def file and .orpc section directive are MSVC-style linker flags.
# With MSVC (cl.exe / clang-cl) they are passed as-is.  With GNU-mode clang
# they must be forwarded to the linker via -Xlinker.
# The LINKER: prefix (CMake 3.26+) is used for the simple /DEF: flag.
# For /SECTION:.orpc,ERW (which contains a comma) we must avoid LINKER:
# because it would split on the comma, so we use explicit if/else.
corehost_target_msvc_linker_options(conhost_proxy PRIVATE
    "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/conhost_proxy.def"
    "/SECTION:.orpc,ERW"
)

if(COREHOST_COMPILER_CLANG_CL)
    # MIDL-generated proxy sources rely on a few Microsoft/COM ABI patterns
    # that trigger warnings under -Wall -Wextra -Wpedantic. Keep the warnings
    # on for the rest of the project, but silence them for the proxy/stub target.
    #
    # clang-cl mode: -clang: prefix forwards GNU-style flags
    # through the clang-cl driver.
    target_compile_options(conhost_proxy PRIVATE
        -clang:-Wno-pragma-pack
        -clang:-Wno-missing-braces
        -clang:-Wno-microsoft-cast
        -clang:-Wno-unused-const-variable
    )
elseif(COREHOST_COMPILER_CLANG)
    # GNU mode (clang.exe / clang++.exe)
    target_compile_options(conhost_proxy PRIVATE
        -Wno-pragma-pack
        -Wno-missing-braces
        -Wno-microsoft-cast
        -Wno-microsoft-redeclare-static
        -Wno-unused-const-variable
    )
endif()

# Super-target used by tests and tools that want the proxy/stub DLL available
# as a standalone build artifact (independent of the test executable).
add_custom_target(conhost_proxy_dll DEPENDS conhost_proxy)
