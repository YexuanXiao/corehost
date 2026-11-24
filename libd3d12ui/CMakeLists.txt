add_library(libd3d12ui STATIC
    d3d12_window_renderer.cpp
)

target_include_directories(libd3d12ui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(libd3d12ui PUBLIC
    common
    d3d12
    dcomp
    dxgi
    user32
)

target_compile_features(libd3d12ui PUBLIC cxx_std_23)
