add_executable(test_vtserializer
    test_vtserializer.cpp
)
target_link_libraries(test_vtserializer PRIVATE libvtserializer)
target_compile_features(test_vtserializer PRIVATE cxx_std_23)
add_test(NAME VtSerializer.Reference COMMAND test_vtserializer)
set_tests_properties(VtSerializer.Reference PROPERTIES LABELS "vtserializer" TIMEOUT 30)