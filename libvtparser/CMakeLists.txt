# libvtparser — platform-neutral VT parser (TARGET: standalone).
add_library(libvtparser STATIC
    simd/ascii_scalar.cpp
    simd/ground_scalar.cpp
    simd/ascii_sse42.cpp
    simd/ascii_avx2.cpp
    simd/ascii_avx10.cpp
    simd/dispatch.cpp
)

target_include_directories(libvtparser PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(libvtparser PUBLIC common unicode::unicode)
