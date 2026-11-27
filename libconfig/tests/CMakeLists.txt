add_executable(test_config
    test_config.cpp
)
target_link_libraries(test_config PRIVATE libconfig)
add_test(NAME Config.Defaults COMMAND test_config)
set_tests_properties(Config.Defaults PROPERTIES LABELS "config" TIMEOUT 30)