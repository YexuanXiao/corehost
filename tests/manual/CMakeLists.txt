# manual/CMakeLists.txt — small manual console checks
# These programs are built but not registered with CTest. Run them under
# corehost/ConPTY and inspect the visible output.
cmake_minimum_required(VERSION 3.30)

add_library(manual_common INTERFACE)
target_include_directories(manual_common INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(manual_common INTERFACE cxx_std_17)
if(MSVC)
    target_compile_options(manual_common INTERFACE /utf-8)
endif()

macro(add_manual_test name src)
    add_executable(${name} ${src})
    target_link_libraries(${name} PRIVATE manual_common)
endmacro()

add_manual_test(manual_001_api_basics api_basics.cpp)
add_manual_test(manual_002_vt_cursor_erase vt_cursor_erase.cpp)
add_manual_test(manual_003_vt_sgr_title vt_sgr_title.cpp)
add_manual_test(manual_004_buffer_scroll buffer_scroll.cpp)
add_manual_test(manual_005_unicode_width unicode_width.cpp)
