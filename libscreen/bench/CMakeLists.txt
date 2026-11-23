add_executable(bench_libscreen
    bench_libscreen.cpp
)
target_link_libraries(bench_libscreen PRIVATE libscreen libconsole)
