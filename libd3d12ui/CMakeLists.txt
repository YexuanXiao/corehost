add_library(libd3d12ui INTERFACE)

target_include_directories(libd3d12ui INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(libd3d12ui INTERFACE
    common
    d3d12
    d3dcompiler
    dcomp
    dxgi
    user32
)

target_compile_features(libd3d12ui INTERFACE cxx_std_23)
