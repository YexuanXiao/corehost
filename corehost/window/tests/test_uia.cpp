#include <Windows.h>
#include <combaseapi.h>
#include <UIAutomationClient.h>
#include <UIAutomationCore.h>

#include <cstdio>
#include <cwchar>
#include <cstdlib>
#include <string>
#include <vector>

namespace
{
struct find_window
{
    DWORD process_id{};
    HWND window{};
};

BOOL CALLBACK find_window_callback(HWND window, LPARAM context)
{
    auto *state = reinterpret_cast<find_window *>(context);
    DWORD process_id{};
    ::GetWindowThreadProcessId(window, &process_id);
    if (process_id == state->process_id && ::IsWindowVisible(window))
    {
        state->window = window;
        return FALSE;
    }
    return TRUE;
}

void release(IUnknown *object) noexcept
{
    if (object != nullptr)
    {
        object->Release();
    }
}
} // namespace

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "usage: test_uia <corehostw.exe>\n");
        return EXIT_FAILURE;
    }

    std::string narrow_path{argv[1]};
    std::wstring path(narrow_path.begin(), narrow_path.end());
    std::wstring command = L"\"" + path + L"\" powershell.exe -NoProfile -Command \"Write-Output UIA-HELLO; Start-Sleep -Seconds 10\"";

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (::CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process) == FALSE)
    {
        std::fprintf(stderr, "CreateProcess failed: %lu\n", ::GetLastError());
        return EXIT_FAILURE;
    }

    ::WaitForInputIdle(process.hProcess, 5000);

    find_window state{};
    state.process_id = process.dwProcessId;
    for (int attempt = 0; attempt < 100 && state.window == nullptr; ++attempt)
    {
        ::EnumWindows(&find_window_callback, reinterpret_cast<LPARAM>(&state));
        if (state.window == nullptr)
        {
            ::Sleep(50);
        }
    }
    if (state.window == nullptr)
    {
        std::fprintf(stderr, "corehostw window not found\n");
        ::TerminateProcess(process.hProcess, 1);
        ::CloseHandle(process.hThread);
        ::CloseHandle(process.hProcess);
        return EXIT_FAILURE;
    }

    if (FAILED(::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)))
    {
        std::fprintf(stderr, "CoInitializeEx failed\n");
        ::TerminateProcess(process.hProcess, 1);
        ::CloseHandle(process.hThread);
        ::CloseHandle(process.hProcess);
        return EXIT_FAILURE;
    }

    int result = EXIT_FAILURE;
    IUIAutomation *automation = nullptr;
    IUIAutomationElement *root = nullptr;
    IUIAutomationTextPattern *text_pattern = nullptr;
    IUIAutomationScrollPattern *scroll_pattern = nullptr;
    IUIAutomationTextRange *range = nullptr;
    BSTR text = nullptr;

    if (SUCCEEDED(::CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                                     IID_PPV_ARGS(&automation))) &&
        SUCCEEDED(automation->ElementFromHandle(state.window, &root)))
    {
        CONTROLTYPEID control_type{};
        bool found = false;
        if (SUCCEEDED(root->get_CurrentControlType(&control_type)) && control_type == UIA_DocumentControlTypeId &&
            SUCCEEDED(root->GetCurrentPatternAs(UIA_TextPatternId, IID_PPV_ARGS(&text_pattern))))
        {
            for (int attempt = 0; attempt < 50 && !found; ++attempt)
            {
                if (text != nullptr)
                {
                    ::SysFreeString(text);
                    text = nullptr;
                }
                release(range);
                range = nullptr;
                if (SUCCEEDED(text_pattern->get_DocumentRange(&range)) && SUCCEEDED(range->GetText(-1, &text)) &&
                    text != nullptr && std::wcsstr(text, L"UIA-HELLO") != nullptr)
                {
                    found = true;
                    break;
                }
                ::Sleep(100);
            }
        }
        if (found && SUCCEEDED(root->GetCurrentPatternAs(UIA_ScrollPatternId, IID_PPV_ARGS(&scroll_pattern))) &&
            scroll_pattern != nullptr)
        {
            result = EXIT_SUCCESS;
        }
        else
        {
            std::fprintf(stderr, "UIA text pattern did not expose UIA-HELLO\n");
        }
    }
    else
    {
        std::fprintf(stderr, "UIA provider unavailable\n");
    }

    if (text != nullptr)
    {
        ::SysFreeString(text);
    }
    release(range);
    release(text_pattern);
    release(scroll_pattern);
    release(root);
    release(automation);
    ::CoUninitialize();

    ::TerminateProcess(process.hProcess, 0);
    ::CloseHandle(process.hThread);
    ::CloseHandle(process.hProcess);
    return result;
}
