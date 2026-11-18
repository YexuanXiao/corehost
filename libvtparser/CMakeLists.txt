# libvtparser — platform-neutral VT parser (TARGET: standalone).
add_library(libvtparser_simd STATIC
    simd/ascii_scalar.cpp
    simd/ground_scalar.cpp
    simd/dispatch.cpp
)

target_include_directories(libvtparser_simd PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/simd
)
target_link_libraries(libvtparser_simd PRIVATE common)
target_compile_features(libvtparser_simd PRIVATE cxx_std_23)
set_target_properties(libvtparser_simd PROPERTIES INTERPROCEDURAL_OPTIMIZATION OFF)

if(COREHOST_SIMD AND COREHOST_COMPILER_MSVC AND CONHOST_PROXY_ARCH_DIR STREQUAL "x64")
    target_sources(libvtparser_simd PRIVATE
        simd/ascii_sse42.cpp
        simd/ascii_avx2.cpp
    )
    target_compile_definitions(libvtparser_simd PRIVATE
        COREHOST_SIMD_HAVE_SSE42=1
        COREHOST_SIMD_HAVE_AVX2=1
    )
    set_source_files_properties(
        simd/ascii_sse42.cpp
        PROPERTIES COMPILE_OPTIONS "/arch:SSE4.2;/GL-"
    )
    set_source_files_properties(
        simd/ascii_avx2.cpp
        PROPERTIES COMPILE_OPTIONS "/arch:AVX2;/GL-"
    )

    if(COREHOST_HAS_AVX10)
        target_sources(libvtparser_simd PRIVATE
            simd/ascii_avx10.cpp
        )
        target_compile_definitions(libvtparser_simd PRIVATE COREHOST_SIMD_HAVE_AVX10=1)
        set_source_files_properties(
            simd/ascii_avx10.cpp
            PROPERTIES COMPILE_OPTIONS "/arch:AVX10.1;/GL-"
        )
    endif()
endif()

add_library(libvtparser INTERFACE)

target_include_directories(libvtparser INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(libvtparser INTERFACE common unicode::unicode libvtparser_simd)

target_compile_features(libvtparser INTERFACE cxx_std_23)
