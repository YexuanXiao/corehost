add_library(libconfig INTERFACE)

target_include_directories(libconfig INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(libconfig INTERFACE
    libcoreui
)

target_compile_features(libconfig INTERFACE cxx_std_23)