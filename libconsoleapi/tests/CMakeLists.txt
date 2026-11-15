add_executable(test_consoleapi
    test_consoleapi.cpp
)
target_link_libraries(test_consoleapi PRIVATE libconsoleapi)
target_compile_features(test_consoleapi PRIVATE cxx_std_23)
add_test(NAME ConsoleAPI.Reference COMMAND test_consoleapi)
set_tests_properties(ConsoleAPI.Reference PROPERTIES LABELS "consoleapi" TIMEOUT 30)