#pragma once

#include "condrv_io.hpp"

#include <concepts>
#include <stop_token>
#include <atomic>

namespace corehost::coreio
{

template <typename Transport>
concept server_transport = requires(Transport &transport, CD_IO_COMPLETE *previous, io_msg &message) {
    { transport.read(previous, message) } -> std::same_as<read_io_result>;
    transport.wait();
};

// Drives the ConDrv server loop. The previous completion is retained in the
// same io_msg and submitted on the next read, keeping one IOCTL per message.
template <server_transport Transport, typename Handler>
void run_server(Transport &transport, Handler &handler, std::stop_token stop = {})
{
    io_msg message{};
    CD_IO_COMPLETE *previous = nullptr;

    for (;;)
    {
        if (stop.stop_requested())
        {
            return;
        }
        const auto result = transport.read(previous, message);
        if (result == read_io_result::got_message)
        {
            if (handler.handle(message))
            {
                previous = &message.complete;
            }
            else
            {
                previous = nullptr;
            }
            handler.flush();
            continue;
        }
        if (result == read_io_result::disconnected)
        {
            return;
        }
        if (stop.stop_requested())
        {
            return;
        }
        transport.wait();
    }
}

template <server_transport Transport, typename Handler>
void run_server(Transport &transport, Handler &handler, const std::atomic_bool &stop)
{
    io_msg message{};
    CD_IO_COMPLETE *previous = nullptr;

    for (;;)
    {
        if (stop.load(std::memory_order_relaxed))
        {
            return;
        }
        const auto result = transport.read(previous, message);
        if (result == read_io_result::got_message)
        {
            if (handler.handle(message))
            {
                previous = &message.complete;
            }
            else
            {
                previous = nullptr;
            }
            handler.flush();
            continue;
        }
        if (result == read_io_result::disconnected)
        {
            return;
        }
        if (stop.load(std::memory_order_relaxed))
        {
            return;
        }
        transport.wait();
    }
}

} // namespace corehost::coreio