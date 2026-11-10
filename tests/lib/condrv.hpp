// tests/lib/condrv.hpp
// 测试进程直接扮演"控制台客户端"：自建 ConDrv 会话并交给 corehost 的 defterm 入口。
//
// 参考 terminal/src/tools/ConsoleBench/conhost.cpp（只读学习）：
// 1. 打开 \Device\ConDrv\Server（可继承）+ \Reference；
// 2. 用 --server / 位置参数把 server 句柄交给 corehost，且只继承这一个句柄；
// 3. 测试进程在 \Reference 下打开 \Connect，EA 里带 CONSOLE_SERVER_MSG —— 这就是
//    客户端侧的 CONNECT；
// 4. 把 connection 句柄写进 PEB 的 ConsoleHandle 槽位，之后本进程的 Console API
//    调用就会走这条连接。
//
// 这样 defterm 测试完全不依赖注册表、默认终端、系统控制台或 GUI。
#pragma once
#include <windows.h>
#include <winternl.h>

#include <cstddef>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>

#include "os/Console/conmsgl1.h"
#include "win32/handle.hpp"

namespace corehost::test
{

struct condrv_session
{
    win32::handle server;
    win32::handle reference;
    win32::handle connection;
};

namespace details
{

inline win32::handle open_condrv_object(win32::handle_view parent, const wchar_t *name, bool inherit, bool synchronous)
{
    UNICODE_STRING object_name{};
    ::RtlInitUnicodeString(&object_name, name);

    OBJECT_ATTRIBUTES attributes{};
    InitializeObjectAttributes(&attributes, &object_name, inherit ? OBJ_INHERIT : 0ul, parent.get(), nullptr);

    IO_STATUS_BLOCK io_status{};
    HANDLE handle = nullptr;
    const NTSTATUS status = ::NtCreateFile(&handle, FILE_GENERIC_READ | FILE_GENERIC_WRITE, &attributes, &io_status,
                                           nullptr, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                           FILE_CREATE, synchronous ? FILE_SYNCHRONOUS_IO_NONALERT : 0ul, nullptr, 0);
    if (status < 0)
    {
        ::SetLastError(::RtlNtStatusToDosError(status));
        return {};
    }
    return win32::handle{handle};
}

} // namespace details

// 创建 ConDrv 会话的服务端对象。server 可被宿主进程继承。
inline bool create_condrv_server(condrv_session &session, DWORD &error)
{
    error = 0;
    session.server = details::open_condrv_object({}, L"\\Device\\ConDrv\\Server", true, false);
    if (!session.server.valid())
    {
        error = ::GetLastError();
        return false;
    }

    session.reference = details::open_condrv_object(session.server.view(), L"\\Reference", false, true);
    if (!session.reference.valid())
    {
        error = ::GetLastError();
        session.server = win32::handle{};
        return false;
    }
    return true;
}

// 启动 corehost 的 defterm 入口：第一个位置参数是 server 句柄。
inline bool launch_defterm_host(std::wstring_view corehost_path, win32::handle_view server, DWORD &error,
                                PROCESS_INFORMATION *process_out = nullptr)
{
    error = 0;

    // 只继承 server 句柄：defterm 入口用 GetCommandLineW 解析位置参数，
    // 句柄值就是命令行里的那个数字。
    HANDLE inherited[1] = {server.get()};

    SIZE_T list_size = 0;
    ::InitializeProcThreadAttributeList(nullptr, 1, 0, &list_size);
    if (list_size == 0)
    {
        error = ::GetLastError();
        return false;
    }

    // 属性表要求指针对齐。
    std::unique_ptr<std::max_align_t[]> storage{
        new std::max_align_t[(list_size + sizeof(std::max_align_t) - 1) / sizeof(std::max_align_t)]};
    auto *attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.get());
    if (::InitializeProcThreadAttributeList(attributes, 1, 0, &list_size) == FALSE)
    {
        error = ::GetLastError();
        return false;
    }

    if (::UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, sizeof(inherited),
                                    nullptr, nullptr) == FALSE)
    {
        error = ::GetLastError();
        ::DeleteProcThreadAttributeList(attributes);
        return false;
    }

    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.lpAttributeList = attributes;

    wchar_t buffer[1024]{};
    ::swprintf_s(buffer, L"\"%.*s\" 0x%llx", static_cast<int>(corehost_path.size()), corehost_path.data(),
                 static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(server.get())));

    PROCESS_INFORMATION process{};
    const BOOL created = ::CreateProcessW(nullptr, buffer, nullptr, nullptr, TRUE, EXTENDED_STARTUPINFO_PRESENT,
                                          nullptr, nullptr, &startup.StartupInfo, &process);
    ::DeleteProcThreadAttributeList(attributes);

    if (created == FALSE)
    {
        error = ::GetLastError();
        return false;
    }

    if (process_out != nullptr)
        *process_out = process;
    else
    {
        ::CloseHandle(process.hThread);
        ::CloseHandle(process.hProcess);
    }
    return true;
}

// 客户端侧 CONNECT：在 \Reference 下打开 \Connect，EA 携带 CONSOLE_SERVER_MSG。
inline bool connect_client(condrv_session &session, std::wstring_view title, std::wstring_view application,
                           bool window_visible, DWORD &error)
{
    error = 0;

    CONSOLE_SERVER_MSG message{};
    message.ProcessGroupId = ::GetCurrentProcessId();
    message.ConsoleApp = TRUE;
    message.WindowVisible = window_visible ? TRUE : FALSE;

    const auto copy_string = [](USHORT &length, auto &buffer, std::wstring_view text) {
        const auto max_chars = sizeof(buffer) / sizeof(wchar_t);
        const auto chars = text.size() < max_chars ? text.size() : max_chars - 1;
        length = static_cast<USHORT>(chars * sizeof(wchar_t));
        std::memcpy(buffer, text.data(), length);
    };
    copy_string(message.TitleLength, message.Title, title);
    copy_string(message.ApplicationNameLength, message.ApplicationName, application);
    copy_string(message.CurrentDirectoryLength, message.CurrentDirectory, L"C:\\Windows\\System32\\");

    // FILE_FULL_EA_INFORMATION 头（忽略末尾可变长 EaName，自行拼接）。
    struct ea_header
    {
        ULONG next_entry_offset;
        UCHAR flags;
        UCHAR ea_name_length;
        USHORT ea_value_length;
    };

    alignas(__STDCPP_DEFAULT_NEW_ALIGNMENT__) std::byte ea_buffer[2048]{};
    std::byte *cursor = ea_buffer;
    const auto append = [&cursor](const auto &value) {
        std::memcpy(cursor, &value, sizeof(value));
        cursor += sizeof(value);
    };

    constexpr UCHAR ea_name_length = 6; // "server" 不含结尾 \0
    append(ea_header{.next_entry_offset = 0,
                     .flags = 0,
                     .ea_name_length = ea_name_length,
                     .ea_value_length = static_cast<USHORT>(sizeof(message))});
    constexpr char ea_name[] = "server";
    std::memcpy(cursor, ea_name, sizeof(ea_name)); // 含结尾 \0
    cursor += sizeof(ea_name);
    append(message);

    UNICODE_STRING object_name{};
    ::RtlInitUnicodeString(&object_name, L"\\Connect");

    OBJECT_ATTRIBUTES attributes{};
    InitializeObjectAttributes(&attributes, &object_name, OBJ_CASE_INSENSITIVE, session.reference.get(), nullptr);

    IO_STATUS_BLOCK io_status{};
    HANDLE connection = nullptr;
    const NTSTATUS status =
        ::NtCreateFile(&connection, FILE_GENERIC_READ | FILE_GENERIC_WRITE, &attributes, &io_status, nullptr, 0,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_CREATE, FILE_SYNCHRONOUS_IO_NONALERT,
                       ea_buffer, static_cast<ULONG>(cursor - ea_buffer));
    if (status < 0)
    {
        error = ::RtlNtStatusToDosError(status);
        return false;
    }

    session.connection = win32::handle{connection};
    return true;
}

// 把连接句柄写入 PEB 的 ConsoleHandle 槽位（RTL_USER_PROCESS_PARAMETERS::Reserved2[0]）。
inline void bind_console_connection(win32::handle_view connection) noexcept
{
    NtCurrentTeb()->ProcessEnvironmentBlock->ProcessParameters->Reserved2[0] = connection.get();
}

inline void unbind_console_connection() noexcept
{
    NtCurrentTeb()->ProcessEnvironmentBlock->ProcessParameters->Reserved2[0] = 0;
}

} // namespace corehost::test
