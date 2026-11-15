#include "condrv_io.hpp"

#include <cstdio>
#include <cstdlib>

int main()
{
    if (corehost::coreio::IOCTL_READ_IO == 0 ||
        corehost::coreio::IOCTL_COMPLETE_IO == 0 ||
        sizeof(corehost::coreio::io_msg) < 4096)
    {
        std::fprintf(stderr, "coreio ABI failed\n");
        return EXIT_FAILURE;
    }

    std::printf("libcoreio tests passed\n");
    return EXIT_SUCCESS;
}