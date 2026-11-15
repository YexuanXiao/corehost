#pragma once

#include "console_api.hpp"

#include "screen_buffer.hpp"
#include "console_state.hpp"

#include "condrv_io.hpp"

namespace corehost::corehost2
{

// Assembly object for the ConDrv server side. It owns only the console-facing
// handler; reading/completion batching stays in libcoreio.
class server_session final
{
  public:
    server_session(screen::screen_buffer &buffer, screen::console_state &state) noexcept
        : api_{buffer, state}
    {
    }

    void handle(coreio::io_msg &message) noexcept
    {
        (void)api_.handle(message);
    }

  private:
    consoleapi::console_api api_;
};

} // namespace corehost::corehost2