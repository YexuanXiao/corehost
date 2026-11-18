add_executable(test_consoleapi
    test_consoleapi.cpp
)
target_link_libraries(test_consoleapi PRIVATE libconsole)
target_compile_definitions(test_consoleapi PRIVATE COREPTY_TESTING)
target_compile_features(test_consoleapi PRIVATE cxx_std_23)
add_test(NAME ConsoleAPI.Reference COMMAND test_consoleapi)
set_tests_properties(ConsoleAPI.Reference PROPERTIES LABELS "consoleapi" TIMEOUT 30)

add_executable(test_consoleinput
    test_consoleinput.cpp
)
target_link_libraries(test_consoleinput PRIVATE libconsole)
target_compile_features(test_consoleinput PRIVATE cxx_std_23)
add_test(NAME ConsoleInput.Reference COMMAND test_consoleinput)
set_tests_properties(ConsoleInput.Reference PROPERTIES LABELS "consoleinput" TIMEOUT 30)
