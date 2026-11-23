#pragma once

// Select the x64 MSVC SIMD implementation in the source files themselves.
// clang-cl and ARM64 take the scalar path without any CMake-level source
// selection.
#if defined(_MSC_VER) && defined(_M_X64) && !defined(__clang__)
    #define COREHOST_X64_SIMD 1
    // Keep ISA-specific functions out of LTCG inlining into SSE4.2 callers.
    #define COREHOST_SIMD_NOINLINE __declspec(noinline)
#else
    #define COREHOST_X64_SIMD 0
    #define COREHOST_SIMD_NOINLINE
#endif
