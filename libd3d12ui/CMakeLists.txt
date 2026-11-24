find_program(FXC_EXECUTABLE NAMES fxc)
if(NOT FXC_EXECUTABLE)
    message(FATAL_ERROR "fxc.exe from the Windows SDK is required to precompile text shaders")
endif()

set(TEXT_SHADER_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/shaders/text.hlsl)
set(TEXT_VERTEX_CSO ${CMAKE_CURRENT_BINARY_DIR}/text_vs.cso)
set(TEXT_PIXEL_CSO ${CMAKE_CURRENT_BINARY_DIR}/text_ps.cso)
set(TEXT_VERTEX_HEADER ${CMAKE_CURRENT_BINARY_DIR}/text_vs.hpp)
set(TEXT_PIXEL_HEADER ${CMAKE_CURRENT_BINARY_DIR}/text_ps.hpp)

add_custom_command(
    OUTPUT ${TEXT_VERTEX_CSO}
    COMMAND ${FXC_EXECUTABLE} /nologo /T vs_5_0 /E VSMain /Fo${TEXT_VERTEX_CSO} ${TEXT_SHADER_SOURCE}
    DEPENDS ${TEXT_SHADER_SOURCE}
    COMMENT "Compiling text vertex shader"
    VERBATIM
)

add_custom_command(
    OUTPUT ${TEXT_PIXEL_CSO}
    COMMAND ${FXC_EXECUTABLE} /nologo /T ps_5_0 /E PSMain /Fo${TEXT_PIXEL_CSO} ${TEXT_SHADER_SOURCE}
    DEPENDS ${TEXT_SHADER_SOURCE}
    COMMENT "Compiling text pixel shader"
    VERBATIM
)

add_custom_command(
    OUTPUT ${TEXT_VERTEX_HEADER}
    COMMAND ${CMAKE_COMMAND}
        -DINPUT=${TEXT_VERTEX_CSO}
        -DOUTPUT=${TEXT_VERTEX_HEADER}
        -DSYMBOL=text_vertex_shader
        -P ${CMAKE_CURRENT_SOURCE_DIR}/embed_shader.cmake
    DEPENDS ${TEXT_VERTEX_CSO}
    VERBATIM
)

add_custom_command(
    OUTPUT ${TEXT_PIXEL_HEADER}
    COMMAND ${CMAKE_COMMAND}
        -DINPUT=${TEXT_PIXEL_CSO}
        -DOUTPUT=${TEXT_PIXEL_HEADER}
        -DSYMBOL=text_pixel_shader
        -P ${CMAKE_CURRENT_SOURCE_DIR}/embed_shader.cmake
    DEPENDS ${TEXT_PIXEL_CSO}
    VERBATIM
)

add_custom_target(libd3d12ui_shaders DEPENDS ${TEXT_VERTEX_HEADER} ${TEXT_PIXEL_HEADER})

add_library(libd3d12ui INTERFACE)

target_include_directories(libd3d12ui INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
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
