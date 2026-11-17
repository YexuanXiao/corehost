add_executable(test_vtparser
    test_vtparser.cpp
)
target_link_libraries(test_vtparser PRIVATE libvtparser)
target_compile_features(test_vtparser PRIVATE cxx_std_23)
add_test(NAME VtParser.Reference COMMAND test_vtparser)
set_tests_properties(VtParser.Reference PROPERTIES LABELS "vtparser" TIMEOUT 30)