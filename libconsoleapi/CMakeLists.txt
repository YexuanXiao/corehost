add_library(libconsoleapi INTERFACE)

target_include_directories(libconsoleapi INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(libconsoleapi INTERFACE libconsole libscreen libinterop libcoreio libconsoleinput common)

target_compile_features(libconsoleapi INTERFACE cxx_std_23)