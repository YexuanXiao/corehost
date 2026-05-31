#include "manual_common.hpp"

int wmain()
{
    init_manual_console();
    section(L"manual_004_buffer_scroll",
            L"Exercises screen-buffer info, fill APIs, text attributes and ScrollConsoleScreenBuffer.");

    CONSOLE_SCREEN_BUFFER_INFO before{};
    ::GetConsoleScreenBufferInfo(out_handle(), &before);
    wprint(L"Current buffer=%dx%d window=%dx%d\n", before.dwSize.X, before.dwSize.Y,
           before.srWindow.Right - before.srWindow.Left + 1, before.srWindow.Bottom - before.srWindow.Top + 1);

    for (SHORT row = 5; row < 10; ++row)
    {
        COORD at{0, row};
        wchar_t ch = static_cast<wchar_t>(L'0' + row - 5);
        ::FillConsoleOutputCharacterW(out_handle(), ch, 40, at, nullptr);
        ::FillConsoleOutputAttribute(out_handle(), static_cast<WORD>(FOREGROUND_GREEN | FOREGROUND_INTENSITY), 40, at,
                                     nullptr);
    }

    SMALL_RECT source{0, 5, 39, 9};
    COORD dest{10, 12};
    CHAR_INFO fill{};
    fill.Char.UnicodeChar = L'.';
    fill.Attributes = FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_RED;
    ::ScrollConsoleScreenBufferW(out_handle(), &source, nullptr, dest, &fill);

    vt(L"\x1b[18;1H");
    check_line(L"Rows 5-9 should move to rows 12-16 starting at column 10.",
               L"FillConsoleOutput*, attributes and ScrollConsoleScreenBuffer are visible.");
    pause_briefly();
    return 0;
}
