#pragma once

#include "config.hpp"
#include "win32/error.hpp"
#include "win32/handle.hpp"
#include "win32/process_information.hpp"
#include "win32/string.hpp"

#include <string>

namespace corehost::window
{

// Launches the console client after the in-process ConDrv server and console
// session exist. The client must inherit the console reference and the
// \Input/\Output client handles.
class window_client_process final
{
  public:
    window_client_process(win32::handle_view server, win32::wcstring_view title, win32::wcstring_view command_line,
                          const corehost_config &config);
    ~window_client_process();

    window_client_process(const window_client_process &) = delete;
    window_client_process &operator=(const window_client_process &) = delete;

    void terminate() noexcept;

    [[nodiscard]] win32::handle_view process() const noexcept
    {
        return process_.h_process.view();
    }

    [[nodiscard]] DWORD process_id() const noexcept
    {
        return process_.process_id;
    }

    [[nodiscard]] bool wait_for(DWORD milliseconds) const noexcept
    {
        return ::WaitForSingleObject(process_.h_process.get(), milliseconds) == WAIT_OBJECT_0;
    }

    void wait()
    {
        if (::WaitForSingleObject(process_.h_process.get(), INFINITE) == WAIT_FAILED)
        {
            win32::throw_last_error();
        }
    }

  private:
    win32::process_information process_;
};

} // namespace corehost::window
