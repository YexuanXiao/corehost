add_library(libpseudowindow INTERFACE)

target_include_directories(libpseudowindow INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(libpseudowindow INTERFACE common libvtparser user32)

