add_library(libcoreui INTERFACE)

target_include_directories(libcoreui INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/common
    ${CMAKE_SOURCE_DIR}/libconfig
)

target_link_libraries(libcoreui INTERFACE
    libconsole
    libd3d12ui
    libscreen
    libscrollbar
    libuitext
    unicode::unicode
    common
)

target_compile_features(libcoreui INTERFACE cxx_std_23)