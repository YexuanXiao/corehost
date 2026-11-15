add_library(libcorehost2 INTERFACE)

target_include_directories(libcorehost2 INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(libcorehost2 INTERFACE libvtparser libvtstate libconsole libscreen)

target_compile_features(libcorehost2 INTERFACE cxx_std_23)