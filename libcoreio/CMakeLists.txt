add_library(libcoreio INTERFACE)

target_include_directories(libcoreio INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(libcoreio INTERFACE common)

