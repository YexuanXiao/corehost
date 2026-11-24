# libconsole — console state, input queues, and API-facing screen operations.
add_library(libconsole INTERFACE)

target_include_directories(libconsole INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/third_parties/deque
)

target_link_libraries(libconsole INTERFACE
    libscreen
    libinterop
    libcoreio
    libvtparser
    libvtstate
    common
)


if(COREHOST_DEFAULT_UTF8)
    target_compile_definitions(libconsole INTERFACE COREHOST_DEFAULT_UTF8)
endif()

if(COREHOST_RUN_AS_UTF8)
    target_compile_definitions(libconsole INTERFACE COREHOST_RUN_AS_UTF8)
endif()
