add_executable(test_vtstate
    test_vtstate.cpp
)
target_link_libraries(test_vtstate PRIVATE libvtstate)
target_compile_features(test_vtstate PRIVATE cxx_std_23)
add_test(NAME VtState.Reference COMMAND test_vtstate)
set_tests_properties(VtState.Reference PROPERTIES LABELS "vtstate" TIMEOUT 30)

add_executable(test_vtserializer
    test_vtserializer.cpp
)
target_link_libraries(test_vtserializer PRIVATE libvtstate)
target_compile_features(test_vtserializer PRIVATE cxx_std_23)
add_test(NAME VtSerializer.Reference COMMAND test_vtserializer)
set_tests_properties(VtSerializer.Reference PROPERTIES LABELS "vtserializer" TIMEOUT 30)
