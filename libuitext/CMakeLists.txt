add_library(libuitext INTERFACE)

target_include_directories(libuitext INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(libuitext INTERFACE
    common
    dwrite
    unicode
)

target_compile_features(libuitext INTERFACE cxx_std_23)
