add_library(libuitext INTERFACE)

target_include_directories(libuitext INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/libconfig
    ${CMAKE_SOURCE_DIR}/third_parties/deque
)

target_link_libraries(libuitext INTERFACE
    common
    dwrite
)

target_compile_features(libuitext INTERFACE cxx_std_23)
