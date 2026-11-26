add_executable(test_text_metrics
    test_text_metrics.cpp
)
target_link_libraries(test_text_metrics PRIVATE libuitext)
add_test(NAME UiText.Metrics COMMAND test_text_metrics)
set_tests_properties(UiText.Metrics PROPERTIES LABELS "uitext" TIMEOUT 30)

add_executable(test_uia
    test_uia.cpp
)
target_link_libraries(test_uia PRIVATE uiautomationcore ole32 oleaut32 user32)
add_dependencies(test_uia corehostw)
add_test(NAME Window.UIA COMMAND test_uia $<TARGET_FILE:corehostw>)
set_tests_properties(Window.UIA PROPERTIES LABELS "window;uia" TIMEOUT 30)
