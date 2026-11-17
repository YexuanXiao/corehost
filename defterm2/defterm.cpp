#include "defterm.hpp"

#include "console_session.hpp"

#include "condrv_io.hpp"
#include "utility/log.hpp"
#include "win32/event.hpp"
#include "win32/handle.hpp"

#include <cassert>
#include <utility>

namespace corehost::defterm2
{

void defterm_entry(std::uintptr_t condrv_handle)
{
    assert(condrv_handle != 0);
    auto server = win32::handle{reinterpret_cast<HANDLE>(condrv_handle)};
    auto input_event = win32::event{win32::create_tag, true, false};
    coreio::set_server_info(server.view(), input_event.view());

    console_session session{server.view(), input_event.view(), 120, 30};
    session.run();
}

} // namespace corehost::defterm2
