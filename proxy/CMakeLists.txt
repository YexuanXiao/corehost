if(CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL "x64" OR CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL "x86_64")
    set(conhost_PROXY_ARCH_DIR "x64")
elseif(CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL "ARM64" OR CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL "aarch64")
    set(conhost_PROXY_ARCH_DIR "arm64")
else()
    message(FATAL_ERROR "Unsupported architecture: ${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}")
endif()

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
target_link_options(conhost_proxy PRIVATE
    "LINKER:/DEF:${CMAKE_CURRENT_SOURCE_DIR}/conhost_proxy.def"
)
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC" OR (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC"))
    target_link_options(conhost_proxy PRIVATE
        /SECTION:.orpc,ERW
    )
else()
    target_link_options(conhost_proxy PRIVATE
        -Xlinker "/SECTION:.orpc,ERW"
    )
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
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
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
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
