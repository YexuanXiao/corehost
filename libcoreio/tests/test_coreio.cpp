#include "condrv_io.hpp"
#include "server_loop.hpp"
#include "win32_transport.hpp"

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

    struct fake_transport
    {
        int reads{};
        corehost::coreio::read_io_result read(CD_IO_COMPLETE *, corehost::coreio::io_msg &) noexcept
        {
            return ++reads <= 2 ? corehost::coreio::read_io_result::got_message
                                : corehost::coreio::read_io_result::disconnected;
        }
        void wait() noexcept {}
    };
    struct fake_handler
    {
        int handled{};
        void operator()(corehost::coreio::io_msg &) noexcept { ++handled; }
    };

    fake_transport transport;
    fake_handler handler;
    corehost::coreio::run_server(transport, handler);
    if (handler.handled != 2)
    {
        std::fprintf(stderr, "server loop failed\n");
        return EXIT_FAILURE;
    }

    std::printf("libcoreio tests passed\n");
    return EXIT_SUCCESS;
}