# libvtparser — platform-neutral VT parser (TARGET: standalone).
add_library(libvtparser INTERFACE)

target_include_directories(libvtparser INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(libvtparser INTERFACE unicode::unicode)

target_compile_features(libvtparser INTERFACE cxx_std_23)