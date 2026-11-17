add_executable(test_vtstate
    test_vtstate.cpp
)
target_link_libraries(test_vtstate PRIVATE libvtstate)
target_compile_features(test_vtstate PRIVATE cxx_std_23)
add_test(NAME VtState.Reference COMMAND test_vtstate)
set_tests_properties(VtState.Reference PROPERTIES LABELS "vtstate" TIMEOUT 30)