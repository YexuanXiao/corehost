add_executable(test_corehost2
    test_corehost2.cpp
)
target_link_libraries(test_corehost2 PRIVATE libcorehost2)
target_compile_definitions(test_corehost2 PRIVATE COREHOST2_TESTING)
target_compile_features(test_corehost2 PRIVATE cxx_std_23)
add_test(NAME CoreHost2.Reference COMMAND test_corehost2)
set_tests_properties(CoreHost2.Reference PROPERTIES LABELS "corehost2" TIMEOUT 30)