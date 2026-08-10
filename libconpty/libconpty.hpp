#pragma once

#ifndef LIBCONPTY_IMPLEMENTATION
#include "winconpty.h"
#else
#define WIN32_NO_STATUS
#include <windows.h>
#undef WIN32_NO_STATUS
#include "winconpty.h"
#include <winternl.h>
#include <winioctl.h>
#include <cstdint>
#include <memory>
#include <new>
#include <string>
#include <algorithm>
#include <cstdio>
#include <ntstatus.h>

#include "win32/handle.hpp"
#include "win32/hresult.hpp"
#include "win32/pipe.hpp"
#include "win32/process_information.hpp"
#include "win32/string.hpp"
#include "shell/shell.hpp"

#endif

namespace console
{

class proc_thread_attribute_list
{
  public:
    proc_thread_attribute_list() noexcept = default;
    ~proc_thread_attribute_list() noexcept
    {
        reset();
    }
    proc_thread_attribute_list(const proc_thread_attribute_list &) = delete;
    proc_thread_attribute_list &operator=(const proc_thread_attribute_list &) = delete;

    HRESULT initialize(DWORD count) noexcept
    {
        reset();

        SIZE_T size = 0;
        ::InitializeProcThreadAttributeList(nullptr, count, 0, &size);
        if (size == 0)
            return HRESULT_FROM_WIN32(::GetLastError());

        _list = static_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(::HeapAlloc(::GetProcessHeap(), HEAP_ZERO_MEMORY, size));
        if (!_list)
            return E_OUTOFMEMORY;

        if (!::InitializeProcThreadAttributeList(_list, count, 0, &size))
        {
            const HRESULT hr = HRESULT_FROM_WIN32(::GetLastError());
            reset();
            return hr;
        }

        return S_OK;
    }

    void reset() noexcept
    {
        if (_list)
        {
            ::DeleteProcThreadAttributeList(_list);
            ::HeapFree(::GetProcessHeap(), 0, _list);
            _list = nullptr;
        }
    }

    LPPROC_THREAD_ATTRIBUTE_LIST &get() noexcept
    {
        return _list;
    }

  private:
    LPPROC_THREAD_ATTRIBUTE_LIST _list = nullptr;
};

} // namespace console

#ifdef LIBCONPTY_IMPLEMENTATION

#ifdef CONPTY_NO_CPP_STDLIB

namespace stdext
{
class exception;
}

void std::_Xlength_error(const char *)
{
    std::abort();
}
#pragma warning(push)
#pragma warning(disable : 4273)
extern std::_Prhand std::_Raise_handler = nullptr;
#pragma warning(pop)

#endif // CONPTY_NO_CPP_STDLIB

namespace console
{

inline HRESULT hresult_from_nt(LONG st) noexcept
{
    return static_cast<HRESULT>(st) | FACILITY_NT_BIT;
}
inline HRESULT hresult_from_last_error() noexcept
{
    return HRESULT_FROM_WIN32(::GetLastError());
}
inline HRESULT hresult_from_bool(BOOL ok) noexcept
{
    return ok ? S_OK : hresult_from_last_error();
}
inline bool nt_success(LONG s) noexcept
{
    return s >= 0;
}

constexpr DWORD kSystemConsoleInformation = 132;
constexpr const wchar_t *kConDrvServer = L"\\Device\\ConDrv\\Server";
constexpr const wchar_t *kConDrvReference = L"\\Reference";
constexpr size_t kInheritedHandlesCount = 4; // server + stdin + stdout + signal

void EnsureDriverLoaded() noexcept
{
    HMODULE ntdll = nullptr;
    if (!::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, L"ntdll.dll", &ntdll) || !ntdll)
        return;
    auto NtSetSystemInformation =
        reinterpret_cast<LONG(NTAPI *)(DWORD, PVOID, ULONG)>(::GetProcAddress(ntdll, "NtSetSystemInformation"));
    if (!NtSetSystemInformation)
        return;
    struct
    {
        ULONG DriverLoaded : 1;
        ULONG Spare : 31;
    } info{};
    info.DriverLoaded = TRUE;
    NtSetSystemInformation(kSystemConsoleInformation, &info, sizeof(info));
}

LONG OpenConDrvHandle(win32::handle &h, win32::wcstring_view name, win32::handle_view rootDir,
                      bool inheritable) noexcept
{
    UNICODE_STRING uname{.Length = static_cast<USHORT>(name.size() * sizeof(wchar_t)),
                         .MaximumLength = static_cast<USHORT>(name.size() * sizeof(wchar_t)),
                         .Buffer = const_cast<PWSTR>(name.data())};
    OBJECT_ATTRIBUTES oa{};
    InitializeObjectAttributes(&oa, &uname, inheritable ? OBJ_INHERIT : 0ul, rootDir.get(), nullptr);
    IO_STATUS_BLOCK iosb{};
    return ::NtCreateFile(h.put(), GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE, &oa, &iosb, nullptr,
                          FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_OPEN, 0ul,
                          nullptr, 0ul);
}

std::wstring FindConsoleHostPath()
{
#ifndef USE_INBOX_CONHOST
    auto modulePath = shell::get_module_dir_path();
    auto modulePathSize = modulePath.size();
    modulePath.append(L"corehost.exe");
    if (shell::file_exists(modulePath.c_str()))
        return modulePath;
    modulePath.resize(modulePathSize);
    modulePath.append(L"OpenConsole.exe");
    if (shell::file_exists(modulePath.c_str()))
        return modulePath;
#endif
    return shell::get_system_conhost_path();
}

std::wstring BuildConhostCommandLine(win32::wcstring_view conhostPath, COORD size,
                                     win32::handle_view signalPipeConhostSide, win32::handle_view serverHandle,
                                     DWORD dwFlags) noexcept
{
    const wchar_t *inheritCursor = (dwFlags & PSEUDOCONSOLE_INHERIT_CURSOR) ? L"--inheritcursor " : L"";
    const wchar_t *ambiguousWide = (dwFlags & PSEUDOCONSOLE_AMBIGUOUS_IS_WIDE) ? L"--ambiguousIsWide " : L"";
    const wchar_t *textMeasurement = L"";
    switch (dwFlags & PSEUDOCONSOLE_GLYPH_WIDTH__MASK)
    {
    case 0x08:
        textMeasurement = L"--textMeasurement graphemes ";
        break;
    case 0x10:
        textMeasurement = L"--textMeasurement wcswidth ";
        break;
    case 0x18:
        textMeasurement = L"--textMeasurement console ";
        break;
    }

    //   "\"...\""                        2
    //   " --headless"                   13
    //   "--inheritcursor "              17
    //   "--ambiguousIsWide "            19 nullable
    //   "--textMeasurement graphemes "  26 nullable
    //   "--width "     + "32767"         8+5=13
    //   " --height "   + "32767"         9+5=14
    //   " --signal 0x" + "%Ix"          12+16=28
    //   " --server 0x" + "%Ix"          12+16=28
    //   '\0'                             1
    //                                ──────
    //                                 161, up to 168
    constexpr size_t kFixedOverhead = 168;
    std::wstring cmd;
    cmd.reserve(conhostPath.size() + kFixedOverhead);
    cmd.resize(conhostPath.size() + kFixedOverhead);
    int n = std::swprintf(cmd.data(), cmd.size(),
                          L"\"%s\" --headless %s%s%s--width %hd --height %hd --signal 0x%Ix --server 0x%Ix",
                          conhostPath.data(), inheritCursor, ambiguousWide, textMeasurement, size.X, size.Y,
                          reinterpret_cast<std::uintptr_t>(signalPipeConhostSide.get()),
                          reinterpret_cast<std::uintptr_t>(serverHandle.get()));
    if (n <= 0)
        return {};
    cmd.resize(static_cast<size_t>(n));
    return cmd;
}

struct pseudo_console
{
    win32::handle hSignal;
    win32::handle hPtyReference;
    win32::handle hConPtyProcess;
};
static_assert(sizeof(::PseudoConsole) == sizeof(pseudo_console));

HRESULT OpenConDrvHandles(win32::handle &serverHandle, win32::handle &referenceHandle) noexcept
{
    LONG st = OpenConDrvHandle(serverHandle, kConDrvServer, nullptr, TRUE);
    if (!nt_success(st))
    {
        EnsureDriverLoaded();
        st = OpenConDrvHandle(serverHandle, kConDrvServer, nullptr, TRUE);
    }
    if (!nt_success(st))
        return hresult_from_nt(st);

    st = OpenConDrvHandle(referenceHandle, kConDrvReference, serverHandle, FALSE);
    if (!nt_success(st))
        return hresult_from_nt(st);
    return S_OK;
}

HRESULT CreateSignalPipe(win32::handle &readPipe, win32::handle &writePipe) noexcept
{
    // 用 NT API 创建匿名管道：读端 overlapped（corehost 用 overlapped I/O
    // 读取信号，不再需要独立信号线程），写端保持同步（ConptyResize-
    // PseudoConsole 等 API 用同步 WriteFile）。
    //
    // DLL 边界不抛异常：错误码重载返回 NTSTATUS，与 OpenConDrvHandle 同风格。
    // 失败时 readPipe/writePipe 不被修改。
    const LONG st = win32::create_overlapped_pipe(readPipe, writePipe);
    if (!nt_success(st))
        return hresult_from_nt(st);

    // 读端必须可继承：经 --signal 命令行传给 corehost 子进程。
    if (!::SetHandleInformation(readPipe.get(), HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT))
        return hresult_from_last_error();
    return S_OK;
}

HRESULT BuildStartupInfoEx(HANDLE (&inherited)[kInheritedHandlesCount], proc_thread_attribute_list &attrList,
                           STARTUPINFOEXW &siEx) noexcept
{
    if (HRESULT hr = attrList.initialize(1); FAILED(hr))
        return hr;

    if (!::UpdateProcThreadAttribute(attrList.get(), 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited,
                                     kInheritedHandlesCount * sizeof(HANDLE), nullptr, nullptr))
        return hresult_from_last_error();

    siEx = {};
    siEx.StartupInfo.cb = sizeof(STARTUPINFOEXW);
    siEx.StartupInfo.hStdInput = inherited[1];
    siEx.StartupInfo.hStdOutput = inherited[2];
    siEx.StartupInfo.hStdError = inherited[2];
    siEx.StartupInfo.dwFlags |= STARTF_USESTDHANDLES;
    siEx.lpAttributeList = attrList.get();
    return S_OK;
}

HRESULT CreatePseudoConsoleImpl(HANDLE hToken, COORD size, HANDLE hInput, HANDLE hOutput, DWORD dwFlags,
                                HPCON *phPC) noexcept
{
    if (!phPC || size.X == 0 || size.Y == 0)
        return E_INVALIDARG;
    *phPC = nullptr;
    if (hToken == INVALID_HANDLE_VALUE)
        hToken = nullptr;

    win32::handle serverHandle;
    win32::handle referenceHandle;
    if (HRESULT hr = OpenConDrvHandles(serverHandle, referenceHandle); FAILED(hr))
        return hr;

    win32::handle signalPipeRead, signalPipeWrite;
    if (HRESULT hr = CreateSignalPipe(signalPipeRead, signalPipeWrite); FAILED(hr))
        return hr;

    auto conhostPath = FindConsoleHostPath();
    auto cmd = BuildConhostCommandLine(conhostPath, size, signalPipeRead, serverHandle, dwFlags);

    if (cmd.empty())
        return E_FAIL;

    proc_thread_attribute_list attrList;
    STARTUPINFOEXW siEx;
    HANDLE inherited[kInheritedHandlesCount] = {serverHandle.get(), hInput, hOutput, signalPipeRead.get()};
    if (HRESULT hr = BuildStartupInfoEx(inherited, attrList, siEx); FAILED(hr))
        return hr;

    win32::process_information pi;
#ifdef _M_IX86
    {
        PVOID redirectionFlag = nullptr;
        LONG st = ::RtlWow64EnableFsRedirectionEx(WOW64_FILE_SYSTEM_DISABLE_REDIRECT, &redirectionFlag);
        if (!nt_success(st))
            return hresult_from_nt(st);

        if (!::CreateProcessAsUserW(hToken, conhostPath.c_str(), cmd.data(), nullptr, nullptr, TRUE,
                                    EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr, &siEx.StartupInfo,
                                    reinterpret_cast<::PPROCESS_INFORMATION>(&pi)))
        {
            ::RtlWow64EnableFsRedirectionEx(redirectionFlag, &redirectionFlag);
            return hresult_from_last_error();
        }
        ::RtlWow64EnableFsRedirectionEx(redirectionFlag, &redirectionFlag);
    }
#else
    if (!::CreateProcessAsUserW(hToken, conhostPath.c_str(), cmd.data(), nullptr, nullptr, TRUE,
                                EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr, &siEx.StartupInfo,
                                reinterpret_cast<::PPROCESS_INFORMATION>(&pi)))
        return hresult_from_last_error();
#endif

    auto pPty = std::make_unique<pseudo_console>();
    pPty->hSignal = std::move(signalPipeWrite);
    pPty->hPtyReference = std::move(referenceHandle);
    pPty->hConPtyProcess = std::move(pi.h_process);
    *phPC = reinterpret_cast<HPCON>(pPty.release());
    return S_OK;
}

} // namespace console

extern "C"
{

    HRESULT WINAPI ConptyCreatePseudoConsole(COORD size, HANDLE hInput, HANDLE hOutput, DWORD dwFlags, HPCON *phPC)
    {
        return ConptyCreatePseudoConsoleAsUser(nullptr, size, hInput, hOutput, dwFlags, phPC);
    }

    HRESULT WINAPI ConptyCreatePseudoConsoleAsUser(HANDLE hToken, COORD size, HANDLE hInput, HANDLE hOutput,
                                                   DWORD dwFlags, HPCON *phPC)
    {
        if (!phPC)
            return E_INVALIDARG;
        *phPC = nullptr;
        if (!win32::handle_view(hInput).valid() && !win32::handle_view(hOutput).valid())
            return E_INVALIDARG;

        win32::handle duplicatedInput;
        win32::handle duplicatedOutput;
        if (!::DuplicateHandle(::GetCurrentProcess(), hInput, ::GetCurrentProcess(), duplicatedInput.put(), 0, TRUE,
                               DUPLICATE_SAME_ACCESS))
            return console::hresult_from_last_error();
        if (!::DuplicateHandle(::GetCurrentProcess(), hOutput, ::GetCurrentProcess(), duplicatedOutput.put(), 0, TRUE,
                               DUPLICATE_SAME_ACCESS))
            return console::hresult_from_last_error();

        return console::CreatePseudoConsoleImpl(hToken, size, duplicatedInput.get(), duplicatedOutput.get(), dwFlags,
                                                phPC);
    }

    HRESULT WINAPI ConptyResizePseudoConsole(HPCON hPC, COORD size)
    {
        if (!hPC || size.X < 0 || size.Y < 0)
            return E_INVALIDARG;
        auto *p = reinterpret_cast<console::pseudo_console *>(hPC);
        unsigned short pkt[3] = {PTY_SIGNAL_RESIZE_WINDOW, static_cast<unsigned short>(size.X),
                                 static_cast<unsigned short>(size.Y)};
        return ::WriteFile(p->hSignal.get(), pkt, sizeof(pkt), nullptr, nullptr) ? S_OK
                                                                                 : console::hresult_from_last_error();
    }

    HRESULT WINAPI ConptyClearPseudoConsole(HPCON hPC, BOOL keepCursorRow)
    {
        if (!hPC)
            return E_INVALIDARG;
        auto *p = reinterpret_cast<console::pseudo_console *>(hPC);
        unsigned short pkt[2] = {PTY_SIGNAL_CLEAR_WINDOW, static_cast<unsigned short>(keepCursorRow ? 1 : 0)};
        return ::WriteFile(p->hSignal.get(), pkt, sizeof(pkt), nullptr, nullptr) ? S_OK
                                                                                 : console::hresult_from_last_error();
    }

    HRESULT WINAPI ConptyShowHidePseudoConsole(HPCON hPC, BOOL show)
    {
        if (!hPC)
            return E_INVALIDARG;
        auto *p = reinterpret_cast<console::pseudo_console *>(hPC);
        unsigned short pkt[2] = {PTY_SIGNAL_SHOWHIDE_WINDOW, static_cast<unsigned short>(show ? 1 : 0)};
        return ::WriteFile(p->hSignal.get(), pkt, sizeof(pkt), nullptr, nullptr) ? S_OK
                                                                                 : console::hresult_from_last_error();
    }

    HRESULT WINAPI ConptyReparentPseudoConsole(HPCON hPC, HWND newParent)
    {
        if (!hPC)
            return E_INVALIDARG;
        auto *p = reinterpret_cast<console::pseudo_console *>(hPC);
#pragma pack(push, 1)
        struct
        {
            const unsigned short id;
            const std::uint64_t hwnd;
        } data{PTY_SIGNAL_REPARENT_WINDOW, static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(newParent))};
#pragma pack(pop)
        return ::WriteFile(p->hSignal.get(), &data, sizeof(data), nullptr, nullptr)
                   ? S_OK
                   : console::hresult_from_last_error();
    }

    VOID WINAPI ConptyClosePseudoConsole(HPCON hPC)
    {
        if (!hPC)
            return;
        std::unique_ptr<console::pseudo_console> p(reinterpret_cast<console::pseudo_console *>(hPC));
    }

    HRESULT WINAPI ConptyReleasePseudoConsole(HPCON hPC)
    {
        if (!hPC)
            return E_INVALIDARG;
        auto *p = reinterpret_cast<console::pseudo_console *>(hPC);
        p->hPtyReference.clear();
        return S_OK;
    }

    HRESULT WINAPI ConptyPackPseudoConsole(HANDLE hProcess, HANDLE hRef, HANDLE hSignal, HPCON *phPC)
    {
        if (!phPC)
            return E_INVALIDARG;
        *phPC = nullptr;
        win32::handle processHandle{hProcess};
        win32::handle refHandle{hRef};
        win32::handle signalHandle{hSignal};
        if (!processHandle.valid() || !refHandle.valid() || !signalHandle.valid())
            return E_INVALIDARG;

        auto pPty = std::make_unique<console::pseudo_console>();
        pPty->hConPtyProcess = std::move(processHandle);
        pPty->hPtyReference = std::move(refHandle);
        pPty->hSignal = std::move(signalHandle);
        *phPC = reinterpret_cast<HPCON>(pPty.release());
        return S_OK;
    }
}
#endif // LIBCONPTY_IMPLEMENTATION
