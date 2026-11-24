add_executable(test_text_metrics
    test_text_metrics.cpp
)
target_link_libraries(test_text_metrics PRIVATE libuitext)
add_test(NAME UiText.Metrics COMMAND test_text_metrics)
set_tests_properties(UiText.Metrics PROPERTIES LABELS "uitext" TIMEOUT 30)
