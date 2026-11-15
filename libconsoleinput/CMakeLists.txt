add_library(libconsoleinput INTERFACE)

target_include_directories(libconsoleinput INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(libconsoleinput INTERFACE libvtparser)

target_compile_features(libconsoleinput INTERFACE cxx_std_23)