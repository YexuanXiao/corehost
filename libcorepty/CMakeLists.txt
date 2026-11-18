add_library(libcorepty_simd STATIC
    simd/legacy_sgr_scalar.cpp
    simd/legacy_sgr_dispatch.cpp
)

target_include_directories(libcorepty_simd PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/simd
)
target_link_libraries(libcorepty_simd PRIVATE common)
target_compile_features(libcorepty_simd PRIVATE cxx_std_23)
set_target_properties(libcorepty_simd PROPERTIES INTERPROCEDURAL_OPTIMIZATION OFF)

if(COREHOST_SIMD AND COREHOST_COMPILER_MSVC AND CONHOST_PROXY_ARCH_DIR STREQUAL "x64")
    target_sources(libcorepty_simd PRIVATE
        simd/legacy_sgr_sse42.cpp
        simd/legacy_sgr_avx2.cpp
    )
    target_compile_definitions(libcorepty_simd PRIVATE
        COREHOST_SIMD_HAVE_SSE42=1
        COREHOST_SIMD_HAVE_AVX2=1
    )
    set_source_files_properties(
        simd/legacy_sgr_sse42.cpp
        PROPERTIES COMPILE_OPTIONS "/arch:SSE4.2;/GL-"
    )
    set_source_files_properties(
        simd/legacy_sgr_avx2.cpp
        PROPERTIES COMPILE_OPTIONS "/arch:AVX2;/GL-"
    )

    if(COREHOST_HAS_AVX10)
        target_sources(libcorepty_simd PRIVATE simd/legacy_sgr_avx10.cpp)
        target_compile_definitions(libcorepty_simd PRIVATE COREHOST_SIMD_HAVE_AVX10=1)
        set_source_files_properties(
            simd/legacy_sgr_avx10.cpp
            PROPERTIES COMPILE_OPTIONS "/arch:AVX10.1;/GL-"
        )
    endif()
endif()

add_library(libcorepty INTERFACE)

target_include_directories(libcorepty INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/third_parties/deque
)

target_link_libraries(libcorepty INTERFACE
    libvtparser
    libvtstate
    libconsole
    libscreen
    libpseudowindow
    libcoreio
    libcorepty_simd
    common
    unicode::unicode
    user32
)

target_compile_features(libcorepty INTERFACE cxx_std_23)
