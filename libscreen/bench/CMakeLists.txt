add_executable(bench_libscreen
    bench_libscreen.cpp
)
target_link_libraries(bench_libscreen PRIVATE libscreen)
target_compile_features(bench_libscreen PRIVATE cxx_std_23)