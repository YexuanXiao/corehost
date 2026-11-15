#pragma once

#include "condrv_io.hpp"
#include "process_registry.hpp"

#include "ntapi/condrv.hpp"
#include "os/Console/conmsgl1.h"
#include "win32/handle.hpp"

#include <Windows.h>

namespace corehost::coreio
{

// ConDrv connection and object state. This mirrors libcorehost's io_state:
// the first CONNECT accepts the shared \Input/\Output handles, later CONNECTs
// join the same console, and CREATE/CLOSE_OBJECT maintain the client-visible
// object ids used by Console API dispatch.
class io_state final
{
  public:
    enum class object_kind
    {
        unknown,
        input,
        output,
        alternate_output,
    };

    io_state(win32::handle_view server, win32::handle_view fixed_input, win32::handle_view fixed_output,
             process_registry &processes) noexcept
        : server_{server}, fixed_input_{fixed_input}, fixed_output_{fixed_output}, processes_{processes}
    {
    }

    [[nodiscard]] win32::handle_view input() const noexcept
    {
        return input_.valid() ? input_.view() : fixed_input_;
    }

    [[nodiscard]] win32::handle_view output() const noexcept
    {
        return output_.valid() ? output_.view() : fixed_output_;
    }

    [[nodiscard]] ULONG_PTR input_id() const noexcept
    {
        return input_id_;
    }

    [[nodiscard]] ULONG_PTR output_id() const noexcept
    {
        return output_id_;
    }

    [[nodiscard]] ULONG_PTR alternate_output_id() const noexcept
    {
        return alternate_output_id_;
    }

    [[nodiscard]] bool connected() const noexcept
    {
        return accepted_;
    }

    void handle_connect(io_msg &message)
    {
        const auto pid = static_cast<DWORD>(message.descriptor.Process);
        DWORD group = pid;
        if (message.descriptor.InputSize >= sizeof(CONSOLE_SERVER_MSG))
        {
            const auto *info = reinterpret_cast<const CONSOLE_SERVER_MSG *>(message.body);
            if (info->ProcessGroupId != 0)
            {
                group = info->ProcessGroupId;
            }
        }
        processes_.add(pid, group);

        if (!connected() && fixed_input_.valid() && fixed_output_.valid())
        {
            input_id_ = reinterpret_cast<ULONG_PTR>(fixed_input_.get());
            output_id_ = reinterpret_cast<ULONG_PTR>(fixed_output_.get());
            accepted_ = true;
            prepare_connection_completion(message, fixed_input_, fixed_output_, static_cast<ULONG_PTR>(pid));
            return;
        }

        if (!connected())
        {
            accept_connection(server_, message, input_, output_);
            input_id_ = reinterpret_cast<ULONG_PTR>(input_.get());
            output_id_ = reinterpret_cast<ULONG_PTR>(output_.get());
            accepted_ = true;
            return;
        }

        prepare_connection_completion(message, input(), output(), static_cast<ULONG_PTR>(pid));
    }

    void handle_disconnect(io_msg &message) noexcept
    {
        processes_.remove(static_cast<DWORD>(message.descriptor.Process));
        prepare_completion(message);
    }

    bool handle_create_object(io_msg &message)
    {
        if (message.descriptor.InputSize < sizeof(CD_CREATE_OBJECT_INFORMATION))
        {
            prepare_completion(message, error::status_invalid_parameter);
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
            break;
        case CD_IO_OBJECT_TYPE_CURRENT_OUTPUT:
            created = condrv::create_client_handle(server_, L"\\Output");
            output_id_ = reinterpret_cast<ULONG_PTR>(created.get());
            break;
        case CD_IO_OBJECT_TYPE_NEW_OUTPUT:
            created = condrv::create_client_handle(server_, L"\\Output");
            alternate_output_id_ = reinterpret_cast<ULONG_PTR>(created.get());
            break;
        default:
            prepare_completion(message, error::status_invalid_parameter);
            return true;
        }

        prepare_completion(message, 0, reinterpret_cast<ULONG_PTR>(created.release()));
        return true;
    }

    void handle_close_object(io_msg &message) noexcept
    {
        const auto id = message.descriptor.Object;
        if (id != 0 && id == input_id_)
        {
            input_id_ = 0;
        }
        if (id != 0 && id == output_id_)
        {
            output_id_ = 0;
        }
        if (id != 0 && id == alternate_output_id_)
        {
            alternate_output_id_ = 0;
        }
        prepare_completion(message);
    }

    [[nodiscard]] object_kind kind_from_object(ULONG_PTR id) const noexcept
    {
        if (id != 0 && id == input_id_)
        {
            return object_kind::input;
        }
        if (id != 0 && id == output_id_)
        {
            return object_kind::output;
        }
        if (id != 0 && id == alternate_output_id_)
        {
            return object_kind::alternate_output;
        }
        if (id != 0 && id == reinterpret_cast<ULONG_PTR>(input().get()))
        {
            return object_kind::input;
        }
        if (id != 0 && id == reinterpret_cast<ULONG_PTR>(output().get()))
        {
            return object_kind::output;
        }
        return object_kind::unknown;
    }

    void handle_raw_flush(io_msg &message) noexcept
    {
        prepare_completion(message);
    }

  private:
    win32::handle_view server_;
    win32::handle_view fixed_input_;
    win32::handle_view fixed_output_;
    win32::handle input_;
    win32::handle output_;
    process_registry &processes_;
    ULONG_PTR input_id_{};
    ULONG_PTR output_id_{};
    ULONG_PTR alternate_output_id_{};
    bool accepted_{};
};

} // namespace corehost::coreio
