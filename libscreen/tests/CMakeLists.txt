add_executable(test_libscreen
    test_libscreen.cpp
)

target_link_libraries(test_libscreen PRIVATE libscreen)
target_compile_features(test_libscreen PRIVATE cxx_std_23)

# MSVC Debug builds link AddressSanitizer dynamically. Copy the runtime next to
# the test executable so CTest can launch it without a developer prompt.
get_filename_component(COREHOST_MSVC_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
foreach(_asan_dll
        clang_rt.asan_dbg_dynamic-x86_64.dll
        clang_rt.asan_dynamic-x86_64.dll)
    if(EXISTS "${COREHOST_MSVC_BIN_DIR}/${_asan_dll}")
        add_custom_command(TARGET test_libscreen POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${COREHOST_MSVC_BIN_DIR}/${_asan_dll}"
                "$<TARGET_FILE_DIR:test_libscreen>/${_asan_dll}"
        )
    endif()
endforeach()

add_test(NAME LibScreen.Reference COMMAND test_libscreen)
set_tests_properties(LibScreen.Reference PROPERTIES LABELS "libscreen;screen16" TIMEOUT 30)

add_executable(test_libscreen_features
    test_libscreen_features.cpp
)
target_link_libraries(test_libscreen_features PRIVATE libscreen)
target_compile_features(test_libscreen_features PRIVATE cxx_std_23)
add_test(NAME LibScreen.Features COMMAND test_libscreen_features)
set_tests_properties(LibScreen.Features PROPERTIES LABELS "libscreen;features" TIMEOUT 30)

foreach(_target test_libscreen test_libscreen_features)
    foreach(_asan_dll
            clang_rt.asan_dbg_dynamic-x86_64.dll
            clang_rt.asan_dynamic-x86_64.dll)
        if(EXISTS "${COREHOST_MSVC_BIN_DIR}/${_asan_dll}")
            add_custom_command(TARGET ${_target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${COREHOST_MSVC_BIN_DIR}/${_asan_dll}"
                    "$<TARGET_FILE_DIR:${_target}>/${_asan_dll}"
            )
        endif()
    endforeach()
endforeach()