add_library(libvtstate INTERFACE)

target_include_directories(libvtstate INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(libvtstate INTERFACE libvtparser libscreen libconsole)

target_compile_features(libvtstate INTERFACE cxx_std_23)