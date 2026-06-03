#pragma once

#include <windows.h>
#include <winternl.h>

#ifdef __cplusplus
extern "C" {
#endif

NTSTATUS NTAPI NtOpenFile(
    _Out_ PHANDLE FileHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ POBJECT_ATTRIBUTES ObjectAttributes,
    _Out_ PIO_STATUS_BLOCK IoStatusBlock,
    _In_ ULONG ShareAccess,
    _In_ ULONG OpenOptions
);

#ifdef __cplusplus
}
#endif