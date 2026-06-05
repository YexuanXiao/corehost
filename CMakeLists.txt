cmake_minimum_required(VERSION 3.30)

project(CoreHost)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CoreHostCompiler)
corehost_configure_global_compiler()

add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)

option(COREHOST_DISABLE_LOG "Disable CoreHost logging" ON)
add_compile_definitions($<$<BOOL:${COREHOST_DISABLE_LOG}>:COREHOST_DISABLE_LOG>)

set(COREHOST_LOG_LEVEL "1" CACHE STRING "CoreHost log level by frequency: 1=low, 2=medium, 3=high")
add_compile_definitions(COREHOST_LOG_LEVEL=${COREHOST_LOG_LEVEL})

option(COREHOST_PERF_DIAG "Enable aggregated CoreHost performance diagnostics" OFF)
add_compile_definitions($<$<BOOL:${COREHOST_PERF_DIAG}>:COREHOST_PERF_DIAG>)

option(COREHOST_ANSI_OPT "Enable table-driven ANSI code page fast paths" OFF)
add_compile_definitions($<$<BOOL:${COREHOST_ANSI_OPT}>:COREHOST_ANSI_OPT>)

option(COREHOST_USE_SYSTEM_ICU "Use system ICU for Unicode character width calculation" ON)

# ── 架构检测 (proxy 子目录使用) ──────────────────────
corehost_detect_arch(CONHOST_PROXY_ARCH_DIR)

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

# ── common (核心基础设施: os/ntapi/win32/com/utility) ──
add_subdirectory(common)

# ── libcorehost (ConPTY 核心静态库) ──────────────────
add_subdirectory(libcorehost)

# ── corehost (宿主可执行文件) ────────────────────────
add_subdirectory(corehost)

# ── libconpty (ConPTY 静态库 + DLL) ──────────────────
add_subdirectory(libconpty)

# ── bench (ConPTY 性能测试工具) ───────────────────────
add_subdirectory(bench)

# ═══════════════════════════════════════════════════════════════
# 测试
# ═══════════════════════════════════════════════════════════════
enable_testing()
add_subdirectory(tests)
