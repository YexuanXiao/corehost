add_library(libvtserializer INTERFACE)

target_include_directories(libvtserializer INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(libvtserializer INTERFACE libscreen unicode::unicode)

target_compile_features(libvtserializer INTERFACE cxx_std_23)