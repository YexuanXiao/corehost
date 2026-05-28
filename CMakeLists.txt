cmake_minimum_required(VERSION 3.30)

project(CoreHost)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    add_compile_options("/utf-8")
    add_compile_options("$<$<CONFIG:Debug>:/fsanitize=address>")
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:DebugDLL>")
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:DebugDLL>")
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_link_options(-static)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
endif()

add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)

add_compile_definitions($<$<BOOL:${DISABLE_COREHOST_LOG}>:COREHOST_DISABLE_LOG>)
set(COREHOST_LOG_LEVEL "1" CACHE STRING "CoreHost log level by frequency: 1=low, 2=medium, 3=high")
add_compile_definitions(COREHOST_LOG_LEVEL=${COREHOST_LOG_LEVEL})

# ── 架构检测 (proxy 子目录使用) ──────────────────────
if(CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL "x64" OR CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL "x86_64")
    set(CONHOST_PROXY_ARCH_DIR "x64")
elseif(CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL "ARM64" OR CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL "aarch64")
    set(CONHOST_PROXY_ARCH_DIR "arm64")
else()
    message(FATAL_ERROR "Unsupported architecture: ${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}")
endif()

# ── 统一输出目录：所有目标输出到 build/$<CONFIG>/ ──
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIG>)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY  ${CMAKE_BINARY_DIR}/$<CONFIG>)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY  ${CMAKE_BINARY_DIR}/$<CONFIG>)

# ═══════════════════════════════════════════════════════════════
# 子项目 — 按依赖顺序
# ═══════════════════════════════════════════════════════════════

# ── libunicode (UTF 转换 + Unicode 属性) ─────────────
add_subdirectory(third_parties/libunicode)

# ── proxy (conhost_proxy.dll + 头文件) ───────────────
add_subdirectory(proxy)

# ── lib (核心基础设施: miniio/os/ntapi/win32/com/utility) ──
add_subdirectory(lib)

# ── libcorehost (ConPTY 核心静态库) ──────────────────
add_subdirectory(libcorehost)

# ── corehost (宿主可执行文件) ────────────────────────
add_subdirectory(corehost)

# ── libconpty (ConPTY 静态库 + DLL) ──────────────────
add_subdirectory(libconpty)

# ═══════════════════════════════════════════════════════════════
# 测试
# ═══════════════════════════════════════════════════════════════
enable_testing()
add_subdirectory(tests)
