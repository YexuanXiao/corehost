#pragma once

#include "server_session.hpp"

#include "condrv_io.hpp"
#include "ntapi/condrv.hpp"

#include "win32/handle.hpp"

namespace corehost::corehost2
{

// ConDrv control-plane dispatcher: CONNECT/CREATE_OBJECT establish the
// input/output objects used by subsequent Console API messages.
class server_dispatcher final
{
  public:
    server_dispatcher(win32::handle_view server, server_session &session) noexcept
        : server_{server}, session_{session}
    {
    }

    bool handle(coreio::io_msg &message)
    {
        switch (message.descriptor.Function)
        {
        case CONSOLE_IO_CONNECT:
            return handle_connect(message);
        case CONSOLE_IO_CREATE_OBJECT:
            return handle_create_object(message);
        case CONSOLE_IO_CLOSE_OBJECT:
            return handle_close_object(message);
        case CONSOLE_IO_DISCONNECT:
            return handle_disconnect(message);
        case CONSOLE_IO_RAW_FLUSH:
            coreio::prepare_completion(message);
            return true;
        case CONSOLE_IO_USER_DEFINED:
            session_.handle(message);
            return true;
        default:
            coreio::prepare_completion(message, coreio::error::status_illegal_function);
            return true;
        }
    }

  private:
    bool handle_connect(coreio::io_msg &message)
    {
        if (!input_.valid() || !output_.valid())
        {
            coreio::accept_connection(server_, message, input_, output_);
            input_id_ = reinterpret_cast<ULONG_PTR>(input_.get());
            session_.processes().add(message.descriptor.Process);
            output_id_ = reinterpret_cast<ULONG_PTR>(output_.get());
            session_.set_input_object(input_id_);
            return true;
        }

        coreio::prepare_connection_completion(message, input_.view(), output_.view(),
                                             message.descriptor.Process);
        return true;
    }

    bool handle_disconnect(coreio::io_msg &message)
    {
        session_.processes().remove(message.descriptor.Process);
        coreio::prepare_completion(message);
        return true;
    }

    bool handle_close_object(coreio::io_msg &message)
    {
        const auto id = static_cast<ULONG_PTR>(message.descriptor.Object);
        if (id != 0 && id == input_id_)
        {
            input_id_ = 0;
            session_.set_input_object(0);
        }
        if (id != 0 && id == output_id_)
        {
            output_id_ = 0;
        }
        coreio::prepare_completion(message);
        return true;
    }

    bool handle_create_object(coreio::io_msg &message)
    {
        if (message.descriptor.InputSize < sizeof(CD_CREATE_OBJECT_INFORMATION))
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }

        const auto *request = reinterpret_cast<const CD_CREATE_OBJECT_INFORMATION *>(message.body);
        auto type = request->ObjectType;
        if (type == CD_IO_OBJECT_TYPE_GENERIC)
        {
            const auto access = request->DesiredAccess & (GENERIC_READ | GENERIC_WRITE);
            if (access == GENERIC_READ)
            {
                type = CD_IO_OBJECT_TYPE_CURRENT_INPUT;
            }
            else if (access == GENERIC_WRITE)
            {
                type = CD_IO_OBJECT_TYPE_CURRENT_OUTPUT;
            }
        }

        win32::handle created;
        switch (type)
        {
        case CD_IO_OBJECT_TYPE_CURRENT_INPUT:
            created = condrv::create_client_handle(server_, L"\\Input");
            input_id_ = reinterpret_cast<ULONG_PTR>(created.get());
            session_.set_input_object(input_id_);
            break;
        case CD_IO_OBJECT_TYPE_CURRENT_OUTPUT:
            created = condrv::create_client_handle(server_, L"\\Output");
            output_id_ = reinterpret_cast<ULONG_PTR>(created.get());
            break;
        case CD_IO_OBJECT_TYPE_NEW_OUTPUT:
            created = condrv::create_client_handle(server_, L"\\Output");
            break;
        default:
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }

        coreio::prepare_completion(message, 0, reinterpret_cast<ULONG_PTR>(created.release()));
        return true;
    }

    win32::handle_view server_;
    server_session &session_;
    win32::handle input_;
    win32::handle output_;
    ULONG_PTR input_id_{};
    ULONG_PTR output_id_{};
};

} // namespace corehost::corehost2