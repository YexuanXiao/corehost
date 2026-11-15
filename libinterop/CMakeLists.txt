add_library(libinterop INTERFACE)

target_include_directories(libinterop INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(libinterop INTERFACE cxx_std_23)