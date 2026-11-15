#pragma once

#include "server_session.hpp"
#include "utility/log.hpp"

#include "condrv_io.hpp"
#include "input_payload.hpp"
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
        : state_{server, condrv_input, condrv_output, session.processes()}, session_{session},
          server_{server}, payload_{server}
    {
    }

    bool handle(coreio::io_msg &message)
    {
        LOG("dispatch func=%lu object=%llu input=%lu output=%lu", message.descriptor.Function,
            static_cast<unsigned long long>(message.descriptor.Object),
            static_cast<unsigned long>(message.descriptor.InputSize),
            static_cast<unsigned long>(message.descriptor.OutputSize));
        if (message.descriptor.Function == CONSOLE_IO_USER_DEFINED &&
            message.descriptor.InputSize >= sizeof(CONSOLE_MSG_HEADER))
        {
            const auto *api = reinterpret_cast<const CONSOLE_MSG_HEADER *>(message.body);
            LOG("dispatch api=0x%08lX desc=%lu", api->ApiNumber, api->ApiDescriptorSize);
        }
        switch (message.descriptor.Function)
        {
        case CONSOLE_IO_CONNECT:
            state_.handle_connect(message);
            session_.set_input_object(state_.input_id());
            session_.set_input_handle(reinterpret_cast<std::uintptr_t>(state_.input().get()));
            session_.set_output_object(state_.output_id());
            session_.set_alternate_object(state_.alternate_output_id());
            LOG("connect input=%llu output=%llu alternate=%llu handle=%p",
                static_cast<unsigned long long>(state_.input_id()),
                static_cast<unsigned long long>(state_.output_id()),
                static_cast<unsigned long long>(state_.alternate_output_id()), state_.input().get());
            return true;
        case CONSOLE_IO_CREATE_OBJECT:
            state_.handle_create_object(message);
            session_.set_input_object(state_.input_id());
            session_.set_input_handle(reinterpret_cast<std::uintptr_t>(state_.input().get()));
            session_.set_output_object(state_.output_id());
            session_.set_alternate_object(state_.alternate_output_id());
            return true;
        case CONSOLE_IO_CLOSE_OBJECT:
            state_.handle_close_object(message);
            session_.set_input_object(state_.input_id());
            session_.set_input_handle(reinterpret_cast<std::uintptr_t>(state_.input().get()));
            session_.set_output_object(state_.output_id());
            session_.set_alternate_object(state_.alternate_output_id());
            return true;
        case CONSOLE_IO_DISCONNECT:
            state_.handle_disconnect(message);
            return true;
        case CONSOLE_IO_RAW_FLUSH:
            state_.handle_raw_flush(message);
            return true;
        case CONSOLE_IO_RAW_WRITE:
        {
            const auto payload = payload_.read(message, 0);
            LOG("raw write bytes=%zu alt=%d", payload.size(),
                session_.screen().alternate_active() ? 1 : 0);
            LOG_HEX("raw output", payload.data(), payload.size() < 160 ? payload.size() : 160);
            session_.write_raw(std::span<const char>{
                reinterpret_cast<const char *>(payload.data()), payload.size()});
            coreio::prepare_completion(message, 0, payload.size());
            LOG("raw write prepared status=0x%08lX info=%llu",
                static_cast<unsigned long>(message.complete.IoStatus.Status),
                static_cast<unsigned long long>(message.complete.IoStatus.Information));
            if (server_.valid())
            {
                coreio::complete_io(server_, message.complete);
                LOG("raw write completed inline");
                return false;
            }
            LOG("raw write deferred (no server)");
            return true;
        }
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
                if (session_.raw_alerted())
                {
                    session_.clear_raw_alerted();
                    coreio::prepare_completion(message, coreio::error::status_alerted, 0, nullptr, 0);
                    return true;
                }
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

    void take_ctrl_events(std::vector<std::pair<DWORD, DWORD>> &output)
    {
        session_.take_ctrl_events(output);
    }

  private:
    coreio::io_state state_;
    server_session &session_;
    win32::handle_view server_;
    coreio::input_payload_reader payload_;
};

} // namespace corehost::corehost2
