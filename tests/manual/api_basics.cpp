#include "manual_common.hpp"

int wmain()
{
    init_manual_console();
    section(L"manual_001_api_basics",
            L"Exercises basic Win32 console APIs: mode, title, cursor position and direct WriteConsole output.");

    DWORD originalMode = 0;
    ::GetConsoleMode(out_handle(), &originalMode);
    wprint(L"GetConsoleMode returned 0x%08lx\n", originalMode);
    ::SetConsoleMode(out_handle(), originalMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);

    const wchar_t *title = L"manual api basics";
    ::SetConsoleTitleW(title);
    wchar_t readTitle[128]{};
    ::GetConsoleTitleW(readTitle, static_cast<DWORD>(sizeof(readTitle) / sizeof(readTitle[0])));
    wprint(L"SetConsoleTitle/GetConsoleTitle: %s\n", readTitle);

    CONSOLE_SCREEN_BUFFER_INFO info{};
    ::GetConsoleScreenBufferInfo(out_handle(), &info);
    wprint(L"Initial buffer=%dx%d cursor=(%d,%d)\n", info.dwSize.X, info.dwSize.Y, info.dwCursorPosition.X,
           info.dwCursorPosition.Y);

    const COORD pos{4, 8};
    ::SetConsoleCursorPosition(out_handle(), pos);
    DWORD written = 0;
    const wchar_t text[] = L"WriteConsoleW placed this line at column 4, row 8.";
    ::WriteConsoleW(out_handle(), text, static_cast<DWORD>(wcslen(text)), &written, nullptr);

    check_line(L"The title should read 'manual api basics' and one sentence should start at row 8.",
               L"Mode/title/cursor/write APIs all visibly take effect.");
    pause_briefly();
    return 0;
}
