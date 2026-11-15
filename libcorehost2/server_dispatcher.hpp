#pragma once

#include "server_session.hpp"

#include "condrv_io.hpp"
#include "io_state.hpp"
#include "ntapi/condrv.hpp"

#include "win32/handle.hpp"

namespace corehost::corehost2
{

// ConDrv control-plane dispatcher. Object lifetime and connection state live
// in libcoreio::io_state, mirroring libcorehost; this class only routes
// messages to the session.
class server_dispatcher final
{
  public:
    server_dispatcher(win32::handle_view server, win32::handle_view condrv_input,
                      win32::handle_view condrv_output, server_session &session) noexcept
        : state_{server, condrv_input, condrv_output, session.processes()}, session_{session}
    {
    }

    bool handle(coreio::io_msg &message)
    {
        switch (message.descriptor.Function)
        {
        case CONSOLE_IO_CONNECT:
            state_.handle_connect(message);
            session_.set_input_object(state_.input_id());
            return true;
        case CONSOLE_IO_CREATE_OBJECT:
            state_.handle_create_object(message);
            session_.set_input_object(state_.input_id());
            session_.set_alternate_object(state_.alternate_output_id());
            return true;
        case CONSOLE_IO_CLOSE_OBJECT:
            state_.handle_close_object(message);
            session_.set_input_object(state_.input_id());
            session_.set_alternate_object(state_.alternate_output_id());
            return true;
        case CONSOLE_IO_DISCONNECT:
            state_.handle_disconnect(message);
            return true;
        case CONSOLE_IO_RAW_FLUSH:
            state_.handle_raw_flush(message);
            return true;
        case CONSOLE_IO_RAW_WRITE:
            session_.write_raw(std::span<const char>{reinterpret_cast<const char *>(message.body),
                                                     static_cast<std::size_t>(message.descriptor.InputSize)});
            coreio::prepare_completion(message);
            return true;
        case CONSOLE_IO_RAW_READ:
        {
            const auto capacity = std::min<std::size_t>(message.descriptor.OutputSize,
                                                         sizeof(message.body));
            if (capacity == 0)
            {
                coreio::prepare_completion(message, 0, 0, message.body, 0);
                return true;
            }
            const auto written = session_.read_raw(
                std::span<char>{reinterpret_cast<char *>(message.body), capacity});
            if (written == 0)
            {
                if (session_.raw_eof())
                {
                    session_.clear_raw_eof();
                    coreio::prepare_completion(message, 0, 0, nullptr, 0);
                    return true;
                }
                return session_.suspend_raw(message);
            }
            coreio::prepare_completion(message, 0, written, message.body, written);
            return true;
        }
        case CONSOLE_IO_USER_DEFINED:
            return session_.handle(message);
        default:
            coreio::prepare_completion(message, coreio::error::status_illegal_function);
            return true;
        }
    }

  private:
    coreio::io_state state_;
    server_session &session_;
};

} // namespace corehost::corehost2
