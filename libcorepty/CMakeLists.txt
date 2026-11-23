add_library(libcorepty STATIC
    simd/legacy_sgr_scalar.cpp
    simd/legacy_sgr_sse42.cpp
    simd/legacy_sgr_avx2.cpp
    simd/legacy_sgr_avx10.cpp
    simd/legacy_sgr_dispatch.cpp
)

target_include_directories(libcorepty PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/third_parties/deque
)

target_link_libraries(libcorepty PUBLIC
    libvtparser
    libvtstate
    libconsole
    libscreen
    libpseudowindow
    libcoreio
    common
    unicode::unicode
    user32
)

