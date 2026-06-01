#include "host_runner.hpp"

// Thin executable entry point. All benchmark behavior lives in the bench/*.hpp
// files so the test runner can be read by feature area instead of as one long
// main.cpp.
int wmain(int argc, wchar_t **argv)
{
    return bench::run(argc, argv);
}
