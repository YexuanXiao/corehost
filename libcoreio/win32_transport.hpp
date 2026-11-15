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
    win32_transport(win32::handle_view server, win32::handle_view event,
                    win32::handle_view stop_event = {}) noexcept
        : server_{server}, event_{event}, stop_event_{stop_event}
    {
    }

    [[nodiscard]] read_io_result read(CD_IO_COMPLETE *previous, io_msg &message)
    {
        return read_io_try(server_, previous, message);
    }

    void wait() const
    {
        HANDLE waits[2]{};
        DWORD count = 0;
        DWORD stop_index = MAXDWORD;
        DWORD event_index = MAXDWORD;
        if (stop_event_.valid())
        {
            stop_index = count;
            waits[count++] = stop_event_.get();
        }
        if (event_.valid())
        {
            event_index = count;
            waits[count++] = event_.get();
        }
        if (count == 0)
        {
            std::abort();
        }
        const auto result = ::WaitForMultipleObjects(count, waits, FALSE, INFINITE);
        if (result == WAIT_OBJECT_0 + stop_index || result == WAIT_OBJECT_0 + event_index)
        {
            return;
        }
        win32::throw_last_error();
    }

  private:
    win32::handle_view server_;
    win32::handle_view event_;
    win32::handle_view stop_event_;
};

} // namespace corehost::coreio