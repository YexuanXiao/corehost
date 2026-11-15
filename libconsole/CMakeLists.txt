# libconsole — console state built on libscreen.
add_library(libconsole INTERFACE)

target_include_directories(libconsole INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(libconsole INTERFACE libscreen)

target_compile_features(libconsole INTERFACE cxx_std_23)