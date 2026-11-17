add_executable(test_consoleinput
    test_consoleinput.cpp
)
target_link_libraries(test_consoleinput PRIVATE libconsoleinput)
target_compile_features(test_consoleinput PRIVATE cxx_std_23)
add_test(NAME ConsoleInput.Reference COMMAND test_consoleinput)
set_tests_properties(ConsoleInput.Reference PROPERTIES LABELS "consoleinput" TIMEOUT 30)