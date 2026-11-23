add_library(libcorepty INTERFACE)

target_include_directories(libcorepty INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/third_parties/deque
)

target_link_libraries(libcorepty INTERFACE
    libvtparser
    libvtstate
    libconsole
    libscreen
    libpseudowindow
    libcoreio
    common
    unicode::unicode
    user32
)
