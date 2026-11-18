add_executable(test_corepty
    test_corepty.cpp
)
target_link_libraries(test_corepty PRIVATE libcorepty)
target_compile_definitions(test_corepty PRIVATE COREPTY_TESTING)
target_compile_features(test_corepty PRIVATE cxx_std_23)
if(COREHOST_SIMD AND COREHOST_COMPILER_MSVC AND CONHOST_PROXY_ARCH_DIR STREQUAL "x64")
    target_compile_definitions(test_corepty PRIVATE
        COREHOST_TEST_HAVE_SSE42=1
        COREHOST_TEST_HAVE_AVX2=1
    )
    if(COREHOST_HAS_AVX10)
        target_compile_definitions(test_corepty PRIVATE COREHOST_TEST_HAVE_AVX10=1)
    endif()
endif()
add_test(NAME CorePty.Reference COMMAND test_corepty)
set_tests_properties(CorePty.Reference PROPERTIES LABELS "corepty" TIMEOUT 30)

add_executable(test_conptyio
    test_conptyio.cpp
)
target_link_libraries(test_conptyio PRIVATE libcorepty)
target_compile_features(test_conptyio PRIVATE cxx_std_23)
add_test(NAME ConptyIO.Reference COMMAND test_conptyio)
set_tests_properties(ConptyIO.Reference PROPERTIES LABELS "conptyio" TIMEOUT 30)
