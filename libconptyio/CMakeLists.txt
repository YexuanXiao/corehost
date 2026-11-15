add_library(libconptyio INTERFACE)

target_include_directories(libconptyio INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(libconptyio INTERFACE common libvtserializer unicode::unicode)

target_compile_features(libconptyio INTERFACE cxx_std_23)