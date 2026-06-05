add_executable(conpty_bench
    main.cpp
)

target_sources(conpty_bench
    PRIVATE
        FILE_SET HEADERS
            FILES
                cjk_terminal_input_test.hpp
                cjk_terminal_input_worker.hpp
                common.hpp
                conpty_session.hpp
                host_runner.hpp
                large_mixed_vt_cjk_output_test.hpp
                large_mixed_vt_cjk_output_worker.hpp
                long_line_three_vt_output_test.hpp
                long_line_three_vt_output_worker.hpp
                powershell_type_realistic_build_output_test.hpp
                scenarios.hpp
                sgr_sequence_matrix_cases.hpp
                sgr_sequence_matrix_test.hpp
                sgr_sequence_matrix_worker.hpp
                terminal_output_worker.hpp
)

target_link_libraries(conpty_bench PRIVATE
    conpty_static
)

target_include_directories(conpty_bench PRIVATE
    ${CMAKE_SOURCE_DIR}
)

