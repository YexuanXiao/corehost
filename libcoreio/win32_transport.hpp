#pragma once

#include "condrv_io.hpp"

#include "win32/handle.hpp"

#include <Windows.h>
#include <cstdlib>

namespace corehost::coreio
{

// Real ConDrv transport for run_server. The server handle and event are views;
// the session owns their lifetime.
class win32_transport final
{
  public:
    win32_transport(win32::handle_view server, win32::handle_view event) noexcept
        : server_{server}, event_{event}
    {
    }

    [[nodiscard]] read_io_result read(CD_IO_COMPLETE *previous, io_msg &message)
    {
        return read_io_try(server_, previous, message);
    }

    void wait() const
    {
        if (!event_.valid())
        {
            std::abort();
        }
        if (::WaitForSingleObject(event_.get(), INFINITE) != WAIT_OBJECT_0)
        {
            win32::throw_last_error();
        }
    }

  private:
    win32::handle_view server_;
    win32::handle_view event_;
};

} // namespace corehost::coreio