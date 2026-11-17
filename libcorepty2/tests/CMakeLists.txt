add_executable(test_corehost2
    test_corehost2.cpp
)
target_link_libraries(test_corehost2 PRIVATE libcorepty2)
target_compile_definitions(test_corehost2 PRIVATE COREHOST2_TESTING)
target_compile_features(test_corehost2 PRIVATE cxx_std_23)
add_test(NAME CoreHost2.Reference COMMAND test_corehost2)
set_tests_properties(CoreHost2.Reference PROPERTIES LABELS "corehost2" TIMEOUT 30)

add_executable(test_conptyio
    test_conptyio.cpp
)
target_link_libraries(test_conptyio PRIVATE libcorepty2)
target_compile_features(test_conptyio PRIVATE cxx_std_23)
add_test(NAME ConptyIO.Reference COMMAND test_conptyio)
set_tests_properties(ConptyIO.Reference PROPERTIES LABELS "conptyio" TIMEOUT 30)
