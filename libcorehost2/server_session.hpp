#pragma once

#include "console_api.hpp"

#include "screen_buffer.hpp"
#include "console_state.hpp"

#include "condrv_io.hpp"
#include "input.hpp"

namespace corehost::corehost2
{

// Assembly object for the ConDrv server side. It owns only the console-facing
// handler; reading/completion batching stays in libcoreio.
class server_session final
{
  public:
    server_session(screen::screen_buffer &buffer, screen::console_state &state,
                   consoleinput::input_queue &input,
                   std::uintptr_t input_object = 0) noexcept
        : input_{input}, api_{buffer, state, &input, input_object, &processes_}
    {
    }

    [[nodiscard]] coreio::process_registry &processes() noexcept
    {
        return processes_;
    }

    void set_input_object(std::uintptr_t object) noexcept
    {
        api_.set_input_object(object);
    }

    void set_alternate_object(std::uintptr_t object) noexcept
    {
        api_.set_alternate_object(object);
    }

    void operator()(coreio::io_msg &message) noexcept
    {
        handle(message);
    }

    void handle(coreio::io_msg &message) noexcept
    {
        (void)api_.handle(message);
    }

  private:
    consoleinput::input_queue &input_;
    coreio::process_registry processes_;
    consoleapi::console_api api_;
};

} // namespace corehost::corehost2