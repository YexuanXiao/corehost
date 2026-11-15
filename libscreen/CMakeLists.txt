# libscreen — 16-byte cell screen buffer reference library (header-only).
add_library(libscreen INTERFACE)

target_include_directories(libscreen INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(libscreen INTERFACE cxx_std_23)