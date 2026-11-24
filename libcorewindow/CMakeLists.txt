add_library(libcorewindow STATIC
    window_host.cpp
)

target_include_directories(libcorewindow PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/common
)

target_link_libraries(libcorewindow PUBLIC
    libd3d12ui
    common
    user32
)

target_compile_features(libcorewindow PUBLIC cxx_std_23)
