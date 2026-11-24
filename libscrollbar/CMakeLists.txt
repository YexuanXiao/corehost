add_library(libscrollbar INTERFACE)
target_include_directories(libscrollbar INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(libscrollbar INTERFACE cxx_std_23)

add_executable(test_scrollbar tests/test_scrollbar.cpp)
target_link_libraries(test_scrollbar PRIVATE libscrollbar)
add_test(NAME Scrollbar.Geometry COMMAND test_scrollbar)
set_tests_properties(Scrollbar.Geometry PROPERTIES LABELS "scrollbar" TIMEOUT 10)
