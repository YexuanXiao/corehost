#include "window_console_session.hpp"

#include "terminal_response.hpp"
#include "utility/log.hpp"
#include "win32/error.hpp"

#include <cassert>
#include <string_view>
#include <utility>

namespace corehost::window
{

window_console_session::window_console_session(win32::handle_view server, console &console,
                                                     win32::srwlock &console_mutex,
                                                     const corehost_config &config)
    : server_{server}, console_{console}, console_mutex_{console_mutex}, processes_{}, io_state_{server, {}, {}, processes_},
      payload_{server}, api_{console_.buffer(), console_.state(), &console_.input(), 0, &processes_,
                        &payload_},
      input_event_{win32::create_tag, true, false}, stop_event_{win32::create_tag, true, false}
{
    auto &state = console_.state();
    state.output_mode = 0x0001 | 0x0002 | 0x0004;
    switch (config.console.text_measurement)
    {
    case text_measurement_mode::console:
        state.measurement = screen::text_measurement::console;
        break;
    case text_measurement_mode::wcswidth:
        state.measurement = screen::text_measurement::wcswidth;
        break;
    case text_measurement_mode::graphemes:
    default:
        state.measurement = screen::text_measurement::graphemes;
        break;
    }
    // Keep the default narrow: edit.exe's box-drawing layout assumes Box
    // Drawing / Block Elements stay one column wide. With ambiguous wide,
    // table borders are expanded and the menu frame breaks.
    // Tracked upstream at https://github.com/microsoft/terminal/issues/20980.
    state.ambiguous_is_wide = config.console.ambiguous_is_wide;
    vt_applier_ = vtstate::state_applier{state.measurement, state.ambiguous_is_wide};
}

window_console_session::~window_console_session()
{
    stop();
}

void window_console_session::start()
{
    assert(server_.valid());
    assert(!thread_.valid());

    coreio::set_server_info(server_, console_.input_event().view());
    thread_ = win32::basic_thread{&window_console_session::thread_entry, this, {.name = L"corehost-window-console"}};
    input_thread_ =
        win32::basic_thread{&window_console_session::input_thread_entry, this, {.name = L"corehost-window-input"}};
}

void window_console_session::stop()
{
    if (!thread_.valid())
    {
        return;
    }

    stop_.store(true, std::memory_order_relaxed);
    stop_event_.set();
    console_.input_event().set();
    if (input_thread_.valid())
    {
        input_thread_.join();
        input_thread_ = {};
    }
    (void)::CancelSynchronousIo(thread_.get());
    thread_.join();
    thread_ = {};
}

DWORD WINAPI window_console_session::thread_entry(LPVOID self)
{
    static_cast<window_console_session *>(self)->run();
    return 0;
}

DWORD WINAPI window_console_session::input_thread_entry(LPVOID self)
{
    static_cast<window_console_session *>(self)->run_input();
    return 0;
}

void window_console_session::run_input()
{
    HANDLE waits[] = {console_.input_event().get(), stop_event_.get()};
    for (;;)
    {
        const auto wait = ::WaitForMultipleObjects(2, waits, FALSE, INFINITE);
        if (wait == WAIT_OBJECT_0 + 1)
        {
            return;
        }
        if (wait == WAIT_FAILED)
        {
            win32::throw_last_error();
        }

        complete_pending();

        std::unique_lock<win32::srwlock> lock{console_mutex_};
        if (console_.input().empty())
        {
            console_.input_event().reset();
        }
    }
}

void window_console_session::apply_vt(std::u16string_view text)
{
    while (!text.empty())
    {
        const auto result = vt_parser_.parse(text);
        LOG2("vt parse id=%d consumed=%zu raw=%zu", static_cast<int>(result.id), result.consumed,
             result.raw_sequence.size());
        if (result.consumed == 0)
        {
            break;
        }
        text.remove_prefix(result.consumed);
        if (result.id == vtparser::vt_message_id::continue_)
        {
            break;
        }
        if (result.raw_sequence.size() > 0)
        {
            LOG2("sequence ids apc=%d dcs=%d", static_cast<int>(vtparser::vt_message_id::apc_sequence),
                 static_cast<int>(vtparser::vt_message_id::dcs_sequence));
        }
        consoleinput::input_event event{};
        if (vtstate::append_terminal_response(result.id, console_.state(), event.text))
        {
            event.type = consoleinput::input_event_type::text;
            LOG2("terminal response size=%zu", event.text.size());
            console_.push_input(std::move(event));
            vt_parser_.reset();
            continue;
        }        if ((result.id == vtparser::vt_message_id::apc_sequence ||
             result.id == vtparser::vt_message_id::dcs_sequence) &&
            vt_applier_.apply_image(result.raw_sequence, console_.buffer(), console_.state()))
        {
            vt_parser_.reset();
            continue;
        }
        (void)vtstate::apply(result.id, result.message, console_.buffer(), console_.state(), vt_applier_);
        vt_parser_.reset();
    }
}

bool window_console_session::handle_write_console(coreio::io_msg &message)
{
    if (message.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_WRITECONSOLE_MSG))
    {
        coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
        return true;
    }

    auto *request = reinterpret_cast<CONSOLE_WRITECONSOLE_MSG *>(message.body + sizeof(CONSOLE_MSG_HEADER));
    const auto payload = payload_.read(message, sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_WRITECONSOLE_MSG));
    LOG2_HEX("write console", payload.data(), payload.size());
    LOG2("write cp=%u unicode=%d", console_.state().output_code_page, request->Unicode);
    if (request->Unicode != FALSE)
    {
        const auto units = payload.size() / sizeof(char16_t);
        const auto *data = reinterpret_cast<const char16_t *>(payload.data());
        apply_vt(std::u16string_view{data, units});
    }
    else
    {
        const auto code_page = console_.state().output_code_page;
        vt_text_.resize(payload.size());
        const auto units = ::MultiByteToWideChar(
            code_page, 0, reinterpret_cast<const char *>(payload.data()), static_cast<int>(payload.size()),
            reinterpret_cast<LPWSTR>(vt_text_.data()), static_cast<int>(vt_text_.size()));
        if (units > 0)
        {
            apply_vt(std::u16string_view{vt_text_.data(), static_cast<std::size_t>(units)});
        }
    }

    request->NumBytes = static_cast<ULONG>(payload.size());
    coreio::prepare_completion(message, 0, sizeof(CONSOLE_WRITECONSOLE_MSG), message.body + sizeof(CONSOLE_MSG_HEADER),
                               sizeof(CONSOLE_WRITECONSOLE_MSG));
    return true;
}

bool window_console_session::handle(coreio::io_msg &message)
{
    std::unique_lock<win32::srwlock> lock{console_mutex_};
    output_event_.set();

    switch (message.descriptor.Function)
    {
    case CONSOLE_IO_CONNECT:
        io_state_.handle_connect(message);
        api_.set_input_object(io_state_.input_id());
        api_.set_input_handle(reinterpret_cast<std::uintptr_t>(io_state_.input().get()));
        api_.set_output_object(io_state_.output_id());
        api_.set_output_handle(reinterpret_cast<std::uintptr_t>(io_state_.output().get()));
        api_.set_alternate_object(io_state_.alternate_output_id());
        coreio::complete_io(server_, message.complete);
        return false;
    case CONSOLE_IO_CREATE_OBJECT:
        io_state_.handle_create_object(message);
        api_.set_input_object(io_state_.input_id());
        api_.set_input_handle(reinterpret_cast<std::uintptr_t>(io_state_.input().get()));
        api_.set_output_object(io_state_.output_id());
        api_.set_output_handle(reinterpret_cast<std::uintptr_t>(io_state_.output().get()));
        api_.set_alternate_object(io_state_.alternate_output_id());
        return true;
    case CONSOLE_IO_CLOSE_OBJECT:
        io_state_.handle_close_object(message);
        api_.set_input_object(io_state_.input_id());
        api_.set_output_object(io_state_.output_id());
        api_.set_alternate_object(io_state_.alternate_output_id());
        return true;
    case CONSOLE_IO_DISCONNECT:
        io_state_.handle_disconnect(message);
        return true;
    case CONSOLE_IO_RAW_FLUSH:
        io_state_.handle_raw_flush(message);
        coreio::complete_io(server_, message.complete);
        return false;
    case CONSOLE_IO_RAW_WRITE: {
        const auto payload = payload_.read(message, 0);
        LOG2_HEX("raw output", payload.data(), payload.size());
        const auto code_page = console_.state().output_code_page;
        LOG2("raw cp=%u bytes=%zu", code_page, payload.size());
        vt_text_.resize(payload.size());
        const auto units = ::MultiByteToWideChar(
            code_page, 0, reinterpret_cast<const char *>(payload.data()), static_cast<int>(payload.size()),
            reinterpret_cast<LPWSTR>(vt_text_.data()), static_cast<int>(vt_text_.size()));
        if (units > 0)
        {
            apply_vt(std::u16string_view{vt_text_.data(), static_cast<std::size_t>(units)});
            LOG2("screen sample %04x %04x %04x %04x", console_.buffer().at(0, 0).primary(),
                 console_.buffer().at(1, 0).primary(), console_.buffer().at(0, 2).primary(),
                 console_.buffer().at(2, 2).primary());
        }
        coreio::prepare_completion(message, 0, payload.size());
        coreio::complete_io(server_, message.complete);
        return false;
    }
    case CONSOLE_IO_RAW_READ:
        return false;
    case CONSOLE_IO_USER_DEFINED: {
        const auto *header = reinterpret_cast<const CONSOLE_MSG_HEADER *>(message.body);
        LOG2("api=%u size=%u", static_cast<unsigned>(header->ApiNumber), static_cast<unsigned>(message.descriptor.InputSize));
        if (header->ApiNumber == ConsolepWriteConsole)
        {
            handle_write_console(message);
            coreio::complete_io(server_, message.complete);
            return false;
        }
        const bool handled = api_.handle(message);
        if (api_.took_input())
        {
            console_.mark_input_read();
        }
        if (handled)
        {
            coreio::complete_io(server_, message.complete);
        }
        else
        {
            const auto kind = api_.take_pending_read_kind();
            if (kind != consoleapi::console_api::pending_read_kind::none)
            {
                LOG2("pending read kind=%d", static_cast<int>(kind));
                pending_message_ = message;
                pending_kind_ = kind;
                pending_active_ = true;
            }
        }
        return false;
    }
    default:
        coreio::prepare_completion(message, coreio::error::status_illegal_function);
        return true;
    }
}

void window_console_session::complete_pending()
{
    if (!pending_active_)
    {
        return;
    }

    LOG2("complete pending kind=%d", static_cast<int>(pending_kind_));
    std::unique_lock<win32::srwlock> lock{console_mutex_};
    bool completed = false;
    if (pending_kind_ == consoleapi::console_api::pending_read_kind::console_input)
    {
        completed = api_.read_console_input(pending_message_, false);
    }
    else
    {
        completed = api_.resume_read(pending_message_);
    }

    if (completed)
    {
        coreio::complete_io(server_, pending_message_.complete);
        pending_active_ = false;
        pending_kind_ = consoleapi::console_api::pending_read_kind::none;
    }
}

void window_console_session::run()
{
    coreio::win32_transport transport{server_, input_event_.view(), stop_event_.view()};
    message_handler handler{*this};
    coreio::run_server(transport, handler, stop_);
}

} // namespace corehost::window
