add_executable(test_vtparser
    test_vtparser.cpp
)
target_link_libraries(test_vtparser PRIVATE libvtparser)
target_compile_features(test_vtparser PRIVATE cxx_std_23)
if(COREHOST_SIMD AND COREHOST_COMPILER_MSVC AND CONHOST_PROXY_ARCH_DIR STREQUAL "x64")
    target_compile_definitions(test_vtparser PRIVATE
        COREHOST_TEST_HAVE_SSE42=1
        COREHOST_TEST_HAVE_AVX2=1
    )
    if(COREHOST_HAS_AVX10)
        target_compile_definitions(test_vtparser PRIVATE COREHOST_TEST_HAVE_AVX10=1)
    endif()
endif()
add_test(NAME VtParser.Reference COMMAND test_vtparser)
set_tests_properties(VtParser.Reference PROPERTIES LABELS "vtparser" TIMEOUT 30)