# libvtstate — parser messages to libscreen state, including VT serialization.
add_library(libvtstate INTERFACE)

target_include_directories(libvtstate INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(libvtstate INTERFACE libvtparser libscreen unicode::unicode)

