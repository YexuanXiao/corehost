#include "window_client_process.hpp"

#include "ntapi/condrv.hpp"
#include "win32/error.hpp"

#include <Windows.h>

#include <array>
#include <cassert>
#include <cstddef>
#include <vector>

namespace corehost::window
{

window_client_process::window_client_process(win32::handle_view server, win32::wcstring_view title,
                                             win32::wcstring_view command_line)
{
    assert(!command_line.empty());

    auto reference = condrv::create_client_handle(server, L"\\Reference");
    auto input = condrv::create_client_handle(server, L"\\Input", true);
    auto output = condrv::create_client_handle(server, L"\\Output", true);

    win32::handle error;
    if (::DuplicateHandle(::GetCurrentProcess(), output.get(), ::GetCurrentProcess(), error.put(), 0, TRUE,
                          DUPLICATE_SAME_ACCESS) == FALSE)
    {
        win32::throw_last_error();
    }

    auto mutable_command_line = std::wstring{command_line.c_str(), command_line.size()};

    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = input.get();
    startup.StartupInfo.hStdOutput = output.get();
    startup.StartupInfo.hStdError = error.get();
    if (!title.empty())
    {
        startup.StartupInfo.lpTitle = const_cast<wchar_t *>(title.c_str());
    }

    SIZE_T attribute_size = 0;
    (void)::InitializeProcThreadAttributeList(nullptr, 2, 0, &attribute_size);
    auto attributes = std::vector<std::byte>(attribute_size);
    startup.lpAttributeList = reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
    if (::InitializeProcThreadAttributeList(startup.lpAttributeList, 2, 0, &attribute_size) == FALSE)
    {
        win32::throw_last_error();
    }

    HANDLE handles[3] = {input.get(), output.get(), error.get()};
    if (::UpdateProcThreadAttribute(startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles,
                                    sizeof(handles), nullptr, nullptr) == FALSE)
    {
        ::DeleteProcThreadAttributeList(startup.lpAttributeList);
        win32::throw_last_error();
    }

    HANDLE reference_value = reference.get();
    if (::UpdateProcThreadAttribute(startup.lpAttributeList, 0, condrv::proc_thread_attribute_console_reference,
                                    &reference_value, sizeof(reference_value), nullptr, nullptr) == FALSE)
    {
        ::DeleteProcThreadAttributeList(startup.lpAttributeList);
        win32::throw_last_error();
    }

    const auto created = ::CreateProcessW(nullptr, mutable_command_line.data(), nullptr, nullptr, TRUE,
                                          EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr, &startup.StartupInfo,
                                          reinterpret_cast<PPROCESS_INFORMATION>(&process_));
    ::DeleteProcThreadAttributeList(startup.lpAttributeList);
    if (created == FALSE)
    {
        win32::throw_last_error();
    }
}

window_client_process::~window_client_process()
{
    terminate();
}

void window_client_process::terminate() noexcept
{
    if (process_.h_process.valid())
    {
        (void)::TerminateProcess(process_.h_process.get(), 0);
    }
}

} // namespace corehost::window
