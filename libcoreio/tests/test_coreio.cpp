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
        bool handle(corehost::coreio::io_msg &) noexcept { ++handled; return true; }
        void flush() noexcept {}
    };

    fake_transport transport;
    fake_handler handler;
    corehost::coreio::run_server(transport, handler);
    if (handler.handled != 2)
    {
        std::fprintf(stderr, "server loop failed\n");
        return EXIT_FAILURE;
    }

    std::stop_source stop_source;
    stop_source.request_stop();
    fake_handler stopped_handler;
    corehost::coreio::run_server(transport, stopped_handler, stop_source.get_token());
    if (stopped_handler.handled != 0)
    {
        std::fprintf(stderr, "server stop failed\n");
        return EXIT_FAILURE;
    }

    struct previous_transport
    {
        int reads{};
        bool saw_previous_after_idle{};
        bool saw_null_after_idle{};
        corehost::coreio::read_io_result read(CD_IO_COMPLETE *previous,
                                               corehost::coreio::io_msg &) noexcept
        {
            ++reads;
            if (reads == 1)
            {
                return corehost::coreio::read_io_result::got_message;
            }
            if (reads == 2)
            {
                saw_previous_after_idle = previous != nullptr;
                return corehost::coreio::read_io_result::no_message;
            }
            saw_null_after_idle = previous == nullptr;
            return corehost::coreio::read_io_result::disconnected;
        }
        void wait() noexcept {}
    };
    previous_transport previous_check;
    fake_handler previous_handler;
    std::atomic_bool previous_stop{};
    corehost::coreio::run_server(previous_check, previous_handler, previous_stop);
    if (!previous_check.saw_previous_after_idle || !previous_check.saw_null_after_idle)
    {
        std::fprintf(stderr, "server loop retained a submitted completion\n");
        return EXIT_FAILURE;
    }

    std::printf("libcoreio tests passed\n");
    return EXIT_SUCCESS;
}