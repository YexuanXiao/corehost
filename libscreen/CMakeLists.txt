# libscreen — 16-byte cell screen buffer reference library (header-only).
add_library(libscreen INTERFACE)

target_include_directories(libscreen INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/libconfig
    ${CMAKE_SOURCE_DIR}/third_parties/deque
)

target_link_libraries(libscreen INTERFACE libinterop libvtparser unicode::unicode)

