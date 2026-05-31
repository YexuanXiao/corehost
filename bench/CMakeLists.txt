add_executable(conpty_bench
    main.cpp
)

target_link_libraries(conpty_bench PRIVATE
    conpty_static
)

target_include_directories(conpty_bench PRIVATE
    ${CMAKE_SOURCE_DIR}
)

if(MSVC)
    target_compile_options(conpty_bench PRIVATE /utf-8 /permissive-)
endif()

