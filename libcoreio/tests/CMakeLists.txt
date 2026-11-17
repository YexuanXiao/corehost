add_executable(test_coreio
    test_coreio.cpp
)
target_link_libraries(test_coreio PRIVATE libcoreio)
target_compile_features(test_coreio PRIVATE cxx_std_23)
add_test(NAME CoreIO.Reference COMMAND test_coreio)
set_tests_properties(CoreIO.Reference PROPERTIES LABELS "coreio" TIMEOUT 30)