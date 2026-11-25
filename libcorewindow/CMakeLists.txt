add_library(libcorewindow INTERFACE)

target_include_directories(libcorewindow INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/common
)

target_link_libraries(libcorewindow INTERFACE
    libd3d12ui
    libscreen
    libuitext
    libscrollbar
    unicode::unicode
    common
    user32
    ole32
    oleaut32
)

target_compile_features(libcorewindow INTERFACE cxx_std_23)
