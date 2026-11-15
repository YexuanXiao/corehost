add_executable(test_conptyio
    test_conptyio.cpp
)
target_link_libraries(test_conptyio PRIVATE libconptyio)
target_compile_features(test_conptyio PRIVATE cxx_std_23)
add_test(NAME ConptyIO.Reference COMMAND test_conptyio)
set_tests_properties(ConptyIO.Reference PROPERTIES LABELS "conptyio" TIMEOUT 30)