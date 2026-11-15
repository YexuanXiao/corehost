#include "libcorehost2/conpty.hpp"

#include "support/com_handoff_bridge.hpp"

#include "condrv_io.hpp"
#include "win32/event.hpp"
#include "win32/handle.hpp"

#include <Windows.h>
#include <shellapi.h>

#include <cstdint>
#include <cwchar>
#include <string>
#include <utility>

#include "utility/crtdbg.hpp"
#include "utility/log.hpp"

namespace
{

struct parsed_arguments
{
    bool headless{};
    bool com_server{};
    bool inherit_cursor{};
    bool ambiguous_is_wide{};
    std::uintptr_t server{};
    std::uintptr_t signal{};
    short width{120};
    short height{30};
    corehost::screen::text_measurement measurement{corehost::screen::text_measurement::graphemes};
};

std::uintptr_t parse_handle(const wchar_t *text) noexcept
{
    if (text == nullptr)
    {
        return 0;
    }
    wchar_t *end = nullptr;
    const auto value = std::wcstoull(text, &end, 0);
    return end == text ? 0 : static_cast<std::uintptr_t>(value);
}

parsed_arguments parse_arguments()
{
    parsed_arguments result;
    int count = 0;
    auto **argv = ::CommandLineToArgvW(::GetCommandLineW(), &count);
    if (argv == nullptr)
    {
        return result;
    }

    for (int i = 1; i < count; ++i)
    {
        const std::wstring_view arg{argv[i]};
        if (arg == L"--headless")
        {
            result.headless = true;
        }
        else if (arg == L"-Embedding")
        {
            result.com_server = true;
        }
        else if (arg == L"--inheritcursor")
        {
            result.inherit_cursor = true;
        }
        else if (arg == L"--ambiguousIsWide")
        {
            result.ambiguous_is_wide = true;
        }
        else if (arg == L"--server" && i + 1 < count)
        {
            result.server = parse_handle(argv[++i]);
        }
        else if (arg == L"--signal" && i + 1 < count)
        {
            result.signal = parse_handle(argv[++i]);
        }
        else if (arg == L"--width" && i + 1 < count)
        {
            result.width = static_cast<short>(std::wcstol(argv[++i], nullptr, 10));
        }
        else if (arg == L"--height" && i + 1 < count)
        {
            result.height = static_cast<short>(std::wcstol(argv[++i], nullptr, 10));
        }
        else if (arg == L"--textMeasurement" && i + 1 < count)
        {
            const std::wstring_view mode{argv[++i]};
            if (mode == L"console")
            {
                result.measurement = corehost::screen::text_measurement::console;
            }
            else if (mode == L"wcswidth")
            {
                result.measurement = corehost::screen::text_measurement::wcswidth;
            }
            else
            {
                result.measurement = corehost::screen::text_measurement::graphemes;
            }
        }
    }

    (void)::LocalFree(argv);
    return result;
}

int run_com_handoff()
{
    corehost::support::com_handoff_raw handoff{};
    if (!corehost::support::acquire_com_handoff(handoff))
    {
        LOG("COM handoff failed");
        return 1;
    }

    auto server = win32::handle::from_uintptr(handoff.server);
    auto event = win32::handle::from_uintptr(handoff.event);
    auto condrv_input = win32::handle::from_uintptr(handoff.condrv_input);
    auto condrv_output = win32::handle::from_uintptr(handoff.condrv_output);
    auto vt_in = win32::handle::from_uintptr(handoff.vt_in);
    auto vt_out = win32::handle::from_uintptr(handoff.vt_out);
    auto signal = win32::handle::from_uintptr(handoff.signal);

    corehost::corehost2::conpty_entry(server.view(), event.view(), std::move(condrv_input), std::move(condrv_output),
                                      vt_in.view(), vt_out.view(), std::move(signal), handoff.width, handoff.height,
                                      false, corehost::screen::text_measurement::graphemes, true);
    return 0;
}

int run_headless(const parsed_arguments &args)
{
    auto server = win32::handle::from_uintptr(args.server);
    LOG("corehost2 headless server=%p stdin=%p stdout=%p", server.get(), ::GetStdHandle(STD_INPUT_HANDLE),
        ::GetStdHandle(STD_OUTPUT_HANDLE));
    auto input_event = win32::event{win32::create_tag, true, false};
    corehost::coreio::set_server_info(server.view(), input_event.view());

    auto signal = win32::handle::from_uintptr(args.signal);
    corehost::corehost2::conpty_entry(
        server.view(), input_event.view(), {}, {}, win32::handle_view{::GetStdHandle(STD_INPUT_HANDLE)},
        win32::handle_view{::GetStdHandle(STD_OUTPUT_HANDLE)}, std::move(signal), args.width, args.height,
        args.inherit_cursor, args.measurement, args.ambiguous_is_wide);
    return 0;
}

} // namespace

int __stdcall wWinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ PWSTR, _In_ int)
{
    utility::suppress_crt_error_dialogs();
    corehost::log::initialize_log();
    LOG("corehost2 process start, cmdline=%ls", ::GetCommandLineW());

    const auto args = parse_arguments();
    if (args.com_server)
    {
        return run_com_handoff();
    }
    if (args.headless)
    {
        return run_headless(args);
    }

    LOG("unsupported corehost2 command line");
    return 1;
}
