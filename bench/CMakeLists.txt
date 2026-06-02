add_executable(conpty_bench
    main.cpp
    common.hpp
    terminal_output_worker.hpp
    large_mixed_vt_cjk_output_worker.hpp
    long_line_three_vt_output_worker.hpp
    cjk_terminal_input_worker.hpp
    sgr_sequence_matrix_cases.hpp
    sgr_sequence_matrix_worker.hpp
    conpty_session.hpp
    large_mixed_vt_cjk_output_test.hpp
    long_line_three_vt_output_test.hpp
    cjk_terminal_input_test.hpp
    powershell_type_realistic_build_output_test.hpp
    sgr_sequence_matrix_test.hpp
    scenarios.hpp
    host_runner.hpp
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
