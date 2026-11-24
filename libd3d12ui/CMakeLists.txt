find_program(DXC_EXECUTABLE NAMES dxc)
if(NOT DXC_EXECUTABLE)
    message(FATAL_ERROR "dxc.exe from the Windows SDK is required to precompile text shaders")
endif()

set(TEXT_SHADER_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/shaders/text.hlsl)
set(TEXT_VERTEX_CSO ${CMAKE_CURRENT_BINARY_DIR}/text_vs.dxil)
set(TEXT_PIXEL_CSO ${CMAKE_CURRENT_BINARY_DIR}/text_ps.dxil)
set(TEXT_VERTEX_HEADER ${CMAKE_CURRENT_BINARY_DIR}/text_vs.hpp)
set(TEXT_PIXEL_HEADER ${CMAKE_CURRENT_BINARY_DIR}/text_ps.hpp)
set(COLOR_PIXEL_CSO ${CMAKE_CURRENT_BINARY_DIR}/color_ps.dxil)
set(COLOR_PIXEL_HEADER ${CMAKE_CURRENT_BINARY_DIR}/color_ps.hpp)

add_custom_command(
    OUTPUT ${TEXT_VERTEX_CSO}
    COMMAND ${DXC_EXECUTABLE} -nologo -T vs_6_0 -E VSMain -Fo ${TEXT_VERTEX_CSO} ${TEXT_SHADER_SOURCE}
    DEPENDS ${TEXT_SHADER_SOURCE}
    COMMENT "Compiling text vertex shader"
    VERBATIM
)

add_custom_command(
    OUTPUT ${TEXT_PIXEL_CSO}
    COMMAND ${DXC_EXECUTABLE} -nologo -T ps_6_0 -E PSMain -Fo ${TEXT_PIXEL_CSO} ${TEXT_SHADER_SOURCE}
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
    OUTPUT ${COLOR_PIXEL_CSO}
    COMMAND ${DXC_EXECUTABLE} -nologo -T ps_6_0 -E PSMain -Fo ${COLOR_PIXEL_CSO} ${CMAKE_CURRENT_SOURCE_DIR}/shaders/color_text.hlsl
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/color_text.hlsl
    COMMENT "Compiling color text pixel shader"
    VERBATIM
)

add_custom_command(
    OUTPUT ${COLOR_PIXEL_HEADER}
    COMMAND ${CMAKE_COMMAND}
        -DINPUT=${COLOR_PIXEL_CSO}
        -DOUTPUT=${COLOR_PIXEL_HEADER}
        -DSYMBOL=color_pixel_shader
        -P ${CMAKE_CURRENT_SOURCE_DIR}/embed_shader.cmake
    DEPENDS ${COLOR_PIXEL_CSO}
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

add_custom_target(libd3d12ui_shaders DEPENDS ${TEXT_VERTEX_HEADER} ${TEXT_PIXEL_HEADER} ${COLOR_PIXEL_HEADER})

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
