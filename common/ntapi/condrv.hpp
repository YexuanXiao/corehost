#pragma once
#include "windows.h"
#include <winternl.h>
#include "openfile.h"
#include "win32/error.hpp"
#include "win32/handle.hpp"
#include "win32/hresult.hpp"
namespace condrv
{
// 打开 ConDrv \Server 句柄（用于 IOCTL 通信）
// 对标原版: g.pDeviceComm = new ConDrvDeviceComm(Server)
//   内部调用 NtOpenFile(\Device\ConDrv\Server, ...)
[[nodiscard]] inline win32::handle open_server()
{
    win32::wcstring_view name{L"\\Device\\ConDrv\\Server"};
    UNICODE_STRING uname{.Length = static_cast<USHORT>(name.size() * sizeof(wchar_t)),
                         .MaximumLength = static_cast<USHORT>(name.size() * sizeof(wchar_t)),
                         .Buffer = const_cast<PWSTR>(name.data())};
    OBJECT_ATTRIBUTES oa{};
    InitializeObjectAttributes(&oa, &uname, OBJ_CASE_INSENSITIVE, nullptr, nullptr);

    win32::handle h;
    IO_STATUS_BLOCK iosb{};

    auto st = ::NtOpenFile(h.put(), GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE, &oa, &iosb,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_SYNCHRONOUS_IO_NONALERT);
    if (st < 0)
        win32::throw_hresult(win32::hresult(HRESULT_FROM_NT(st)));

    return h;
}

// 向 ConDrv 创建客户端句柄（\Input / \Output / \Reference）
[[nodiscard]] inline win32::handle create_client_handle(win32::handle_view server, win32::wcstring_view name)
{
    UNICODE_STRING uname{.Length = static_cast<USHORT>(name.size() * sizeof(wchar_t)),
                         .MaximumLength = static_cast<USHORT>(name.size() * sizeof(wchar_t)),
                         .Buffer = const_cast<PWSTR>(name.data())};

    OBJECT_ATTRIBUTES oa{};
    InitializeObjectAttributes(&oa, &uname, OBJ_CASE_INSENSITIVE, server.get(), nullptr);

    win32::handle h;
    IO_STATUS_BLOCK iosb{};

    auto st = ::NtOpenFile(h.put(), GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE, &oa, &iosb,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_SYNCHRONOUS_IO_NONALERT);
    if (st < 0)
        win32::throw_hresult(win32::hresult(HRESULT_FROM_NT(st)));

    return h;
}
} // namespace condrv
