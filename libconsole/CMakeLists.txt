# libconsole — console state built on libscreen.
add_library(libconsole INTERFACE)

target_include_directories(libconsole INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(libconsole INTERFACE libscreen)

target_compile_features(libconsole INTERFACE cxx_std_23)

if(COREHOST_DEFAULT_UTF8)
    target_compile_definitions(libconsole INTERFACE COREHOST_DEFAULT_UTF8)
endif()

if(COREHOST_RUN_AS_UTF8)
    target_compile_definitions(libconsole INTERFACE COREHOST_RUN_AS_UTF8)
endif()