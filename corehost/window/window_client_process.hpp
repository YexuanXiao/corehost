#pragma once

#include "win32/handle.hpp"
#include "win32/process_information.hpp"

#include <string>
#include <string_view>

namespace corehost::window
{

// Launches the console client after the in-process ConDrv server and console
// session exist. The client must inherit the console reference and the
// \Input/\Output client handles.
class window_client_process final
{
  public:
    window_client_process(win32::handle_view server, std::wstring_view title, const std::wstring &command_line);
    ~window_client_process();

    window_client_process(const window_client_process &) = delete;
    window_client_process &operator=(const window_client_process &) = delete;

    void terminate() noexcept;

  private:
    win32::process_information process_;
};

} // namespace corehost::window
