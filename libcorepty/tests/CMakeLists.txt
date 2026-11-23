add_executable(test_corepty
    test_corepty.cpp
)
target_link_libraries(test_corepty PRIVATE libcorepty)
target_compile_definitions(test_corepty PRIVATE COREPTY_TESTING)
add_test(NAME CorePty.Reference COMMAND test_corepty)
set_tests_properties(CorePty.Reference PROPERTIES LABELS "corepty" TIMEOUT 30)

add_executable(test_conptyio
    test_conptyio.cpp
)
target_link_libraries(test_conptyio PRIVATE libcorepty)
add_test(NAME ConptyIO.Reference COMMAND test_conptyio)
set_tests_properties(ConptyIO.Reference PROPERTIES LABELS "conptyio" TIMEOUT 30)
