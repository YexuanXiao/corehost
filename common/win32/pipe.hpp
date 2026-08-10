#pragma once

// ── Win32 匿名管道创建 ────────────────────────────────────
//
// create_overlapped_pipe: 用 NT API 创建匿名管道，读端支持 overlapped I/O。
//
// MSDN 声称匿名管道不支持异步 (overlapped) 读写，实际上只是因为 Win32 API
// 的 CreatePipe 没有提供传入 FILE_FLAG_OVERLAPPED 的途径；直接用
// NtCreateNamedPipeFile/NtCreateFile 创建即可（方法来源见 pipe.txt）。
//
// 设计：
// - 读端 (server 端) 为 overlapped 句柄：CreateOptions = 0。
// - 写端 (client 端) 保持同步：CreateOptions 含 FILE_SYNCHRONOUS_IO_NONALERT，
//   兼容现有同步 WriteFile 调用方（WT 终端、libconpty API）。
// - 句柄默认不可继承；需要跨进程传递时由调用方 SetHandleInformation。

#include <windows.h>
#include <winternl.h>

#include <utility>

#include "win32/error.hpp"
#include "win32/handle.hpp"

// ntdll 导出的 NtCreateNamedPipeFile 是 14 参数版本（不含 AllocationSize/
// FileAttributes；ntifs.h 的 16 参数版本只供内核驱动使用）。
extern "C" NTSTATUS NTAPI NtCreateNamedPipeFile(
    _Out_ PHANDLE FileHandle, _In_ ACCESS_MASK DesiredAccess, _In_ POBJECT_ATTRIBUTES ObjectAttributes,
    _Out_ PIO_STATUS_BLOCK IoStatusBlock, _In_ ACCESS_MASK ShareAccess, _In_ ULONG CreateDisposition,
    _In_ ULONG CreateOptions, _In_ ULONG NamedPipeType, _In_ ULONG ReadMode, _In_ ULONG CompletionMode,
    _In_ ULONG MaximumInstances, _In_ ULONG InboundQuota, _In_ ULONG OutboundQuota, _In_ PLARGE_INTEGER DefaultTimeout);

// winternl.h 未提供管道类型/模式/完成模式常量；取值与 ntifs.h 一致。
#ifndef FILE_PIPE_BYTE_STREAM_TYPE
#define FILE_PIPE_BYTE_STREAM_TYPE 0x00000000
#endif
#ifndef FILE_PIPE_BYTE_STREAM_MODE
#define FILE_PIPE_BYTE_STREAM_MODE 0x00000000
#endif
#ifndef FILE_PIPE_QUEUE_OPERATION
#define FILE_PIPE_QUEUE_OPERATION 0x00000000
#endif

namespace win32
{

// ── overlapped 匿名管道 ────────────────────────────────────
// read  : overlapped 读端（server 端），配合 win32/overlapped.hpp 使用。
// write : 同步写端（client 端），普通 WriteFile 即可。
struct overlapped_pipe
{
    win32::handle read;
    win32::handle write;
};

// 把 NTSTATUS 映射为 Win32 错误码并抛出；无法映射时兜底 GEN_FAILURE。
[[noreturn]] inline void throw_nt_status(NTSTATUS status)
{
    const auto dos = ::RtlNtStatusToDosError(status);
    throw win32::error{dos != 0 ? dos : static_cast<unsigned>(win32::error::gen_failure)};
}

// ── 创建 overlapped 读端 + 同步写端的匿名管道 ──────────────
// 方向固定为 INBOUND（read 端可读、write 端可写）。
//
// 两个重载：
// - create_overlapped_pipe(read, write) : 错误码版本（noexcept），跨模块边界
//   使用（libconpty DLL 导出 HRESULT API，与 OpenConDrvHandle 同一风格）。
//   失败时 read/write 不被修改。
// - create_overlapped_pipe()            : 抛异常版本，corehost 进程内部使用。
[[nodiscard]] inline LONG create_overlapped_pipe(win32::handle &read, win32::handle &write) noexcept
{
    // 1 秒默认超时（100ns 单位，负值表示相对时间）。
    LARGE_INTEGER timeout{.QuadPart = -10'000'0000};
    UNICODE_STRING empty_path{};
    IO_STATUS_BLOCK status_block{};
    OBJECT_ATTRIBUTES object_attributes{
        .Length = sizeof(OBJECT_ATTRIBUTES),
        .ObjectName = &empty_path,
        .Attributes = OBJ_CASE_INSENSITIVE,
    };

    // 打开 \Device\NamedPipe\ 目录。信号管道创建是低频一次性操作，不需要
    // 缓存目录句柄；RAII 在函数退出时自动关闭。
    constexpr wchar_t dir_path[] = L"\\Device\\NamedPipe\\";
    UNICODE_STRING path{};
    path.Length = static_cast<USHORT>(sizeof(dir_path) - sizeof(wchar_t));
    path.MaximumLength = static_cast<USHORT>(sizeof(dir_path));
    path.Buffer = const_cast<PWSTR>(dir_path);
    OBJECT_ATTRIBUTES dir_attributes{
        .Length = sizeof(OBJECT_ATTRIBUTES),
        .ObjectName = &path,
    };
    win32::handle pipe_directory;
    {
        IO_STATUS_BLOCK iosb{};
        const NTSTATUS st = ::NtCreateFile(pipe_directory.put(), SYNCHRONIZE | GENERIC_READ, &dir_attributes, &iosb,
                                           nullptr, FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                           FILE_OPEN, FILE_SYNCHRONOUS_IO_NONALERT, nullptr, 0);
        if (st < 0)
            return st;
    }

    // server 端：PIPE_ACCESS_INBOUND 的读端。CreateOptions=0 → overlapped。
    win32::handle server;
    object_attributes.RootDirectory = pipe_directory.get();
    {
        IO_STATUS_BLOCK iosb{};
        const NTSTATUS st =
            ::NtCreateNamedPipeFile(server.put(),
                                    /* DesiredAccess     */ SYNCHRONIZE | GENERIC_READ | FILE_WRITE_ATTRIBUTES,
                                    /* ObjectAttributes  */ &object_attributes,
                                    /* IoStatusBlock     */ &iosb,
                                    /* ShareAccess       */ FILE_SHARE_WRITE,
                                    /* CreateDisposition */ FILE_CREATE,
                                    /* CreateOptions     */ 0, // FILE_SYNCHRONOUS_IO_NONALERT → 同步管道
                                    /* NamedPipeType     */ FILE_PIPE_BYTE_STREAM_TYPE,
                                    /* ReadMode          */ FILE_PIPE_BYTE_STREAM_MODE,
                                    /* CompletionMode    */ FILE_PIPE_QUEUE_OPERATION,
                                    /* MaximumInstances  */ 1,
                                    /* InboundQuota      */ 0,
                                    /* OutboundQuota     */ 0,
                                    /* DefaultTimeout    */ &timeout);
        if (st < 0)
            return st;
    }

    // client 端：PIPE_ACCESS_INBOUND 的写端。FILE_SYNCHRONOUS_IO_NONALERT → 同步。
    win32::handle client;
    object_attributes.RootDirectory = server.get();
    {
        IO_STATUS_BLOCK iosb{};
        const NTSTATUS st =
            ::NtCreateFile(client.put(),
                           /* DesiredAccess     */ SYNCHRONIZE | GENERIC_WRITE | FILE_READ_ATTRIBUTES,
                           /* ObjectAttributes  */ &object_attributes,
                           /* IoStatusBlock     */ &iosb,
                           /* AllocationSize    */ nullptr,
                           /* FileAttributes    */ 0,
                           /* ShareAccess       */ FILE_SHARE_READ,
                           /* CreateDisposition */ FILE_OPEN,
                           /* CreateOptions     */ FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT,
                           /* EaBuffer          */ nullptr,
                           /* EaLength          */ 0);
        if (st < 0)
            return st;
    }

    read = std::move(server);
    write = std::move(client);
    // STATUS_SUCCESS 恒为 0；直接返回 0 避免引入 ntstatus.h（与 winternl.h
    // 的 STATUS_IN_PAGE_ERROR 宏重定义冲突，产生 C4005 警告）。
    return 0;
}

// 抛异常版本（corehost 进程内部使用）：失败抛 win32::error。
[[nodiscard]] inline overlapped_pipe create_overlapped_pipe()
{
    overlapped_pipe p;
    const LONG st = create_overlapped_pipe(p.read, p.write);
    if (st < 0)
        throw_nt_status(st);
    return p;
}

} // namespace win32
