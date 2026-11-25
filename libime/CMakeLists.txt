add_library(libime INTERFACE)

target_include_directories(libime INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/common
)

target_link_libraries(libime INTERFACE
    ole32
    oleaut32
    user32
)

target_compile_features(libime INTERFACE cxx_std_23)