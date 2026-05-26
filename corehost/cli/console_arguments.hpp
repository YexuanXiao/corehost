#pragma once

#include "win32/command_line.hpp"
#include "text_measurement_mode.hpp"

#include <cstdint>
#include <limits>
#include <string>
#include <algorithm>

namespace console
{

enum class parse_error : int
{
    invalid_handle_value = 1,
    duplicate_condrv_handle,
    duplicate_server_handle,
    missing_server_value,
    duplicate_signal_handle,
    missing_signal_value,
    missing_width_value,
    invalid_size_value,
    missing_height_value,
    missing_feature_value,
    unsupported_feature_value,
    invalid_text_measurement_value,
    missing_text_measurement_value,
};

inline constexpr win32::wcstring_view vt_mode_arg = L"--vtmode";
inline constexpr win32::wcstring_view headless_arg = L"--headless";
inline constexpr win32::wcstring_view server_handle_arg = L"--server";
inline constexpr win32::wcstring_view signal_handle_arg = L"--signal";
inline constexpr win32::wcstring_view handle_prefix = L"0x";
inline constexpr win32::wcstring_view client_commandline_arg = L"--";
inline constexpr win32::wcstring_view force_v1_arg = L"-ForceV1";
inline constexpr win32::wcstring_view force_no_handoff_arg = L"-ForceNoHandoff";
inline constexpr win32::wcstring_view filepath_leader_prefix = L"\\??\\";
inline constexpr win32::wcstring_view width_arg = L"--width";
inline constexpr win32::wcstring_view height_arg = L"--height";
inline constexpr win32::wcstring_view inherit_cursor_arg = L"--inheritcursor";
inline constexpr win32::wcstring_view feature_arg = L"--feature";
inline constexpr win32::wcstring_view feature_pty_arg = L"pty";
inline constexpr win32::wcstring_view com_server_arg = L"-Embedding";
inline constexpr win32::wcstring_view ambiguous_is_wide_arg = L"--ambiguousIsWide";
inline constexpr win32::wcstring_view text_measurement_arg = L"--textMeasurement";
inline constexpr win32::wcstring_view text_graphemes = L"graphemes";
inline constexpr win32::wcstring_view text_wcswidth = L"wcswidth";
inline constexpr win32::wcstring_view text_console = L"console";

class console_arguments
{
  public:
    console_arguments() noexcept = default;

    explicit console_arguments(win32::wcstring_view command_line) : _original_command_line(command_line)
    {
        win32::command_line_view parser(command_line);

        // 丢弃第一个参数，即模块名
        parser.next();

        while (!parser.done())
        {
            // 保存当前剩余原始命令行，用于客户命令
            const win32::wcstring_view remaining = parser.remain();
            const std::wstring_view token = parser.next(); // 解析当前 token

            // 传统句柄值，该值由ConDrv设置
            if (token.starts_with(handle_prefix))
            {
                if (_condrv_handle != 0)
                    throw parse_error::duplicate_condrv_handle;
                _condrv_handle = parse_handle_value(token.substr(2));
                continue;
            }

            // ConPty
            if (token == server_handle_arg)
            {
                if (_server_handle != 0)
                    throw parse_error::duplicate_server_handle;

                std::wstring_view value;
                if (parser.done())
                    throw parse_error::missing_server_value;
                value = parser.next();
                if (!value.starts_with(handle_prefix))
                    throw parse_error::invalid_handle_value;
                _server_handle = parse_handle_value(value.substr(2));
                continue;
            }

            if (token == signal_handle_arg)
            {
                if (_signal_handle != 0)
                    throw parse_error::duplicate_signal_handle;
                if (parser.done())
                    throw parse_error::missing_signal_value;
                auto sigval = parser.next();
                if (!sigval.starts_with(handle_prefix))
                    throw parse_error::invalid_handle_value;
                _signal_handle = parse_handle_value(sigval.substr(2));
                continue;
            }

            if (token == force_v1_arg)
            {
                _force_v1 = true;
                continue;
            }
            if (token == force_no_handoff_arg)
            {
                _force_no_handoff = true;
                continue;
            }
            if (token == com_server_arg)
            {
                _run_as_com_server = true;
                continue;
            }
            if (token.starts_with(filepath_leader_prefix))
            {
                continue;
            }

            if (token == width_arg)
            {
                if (parser.done())
                    throw parse_error::missing_width_value;
                _width = parse_size_value(parser.next());
                continue;
            }
            if (token == height_arg)
            {
                if (parser.done())
                    throw parse_error::missing_height_value;
                _height = parse_size_value(parser.next());
                continue;
            }

            if (token == feature_arg)
            {
                if (parser.done())
                    throw parse_error::missing_feature_value;
                if (parser.next() != feature_pty_arg)
                    throw parse_error::unsupported_feature_value;
                continue;
            }

            if (token == headless_arg)
            {
                _headless = true;
                continue;
            }
            if (token == vt_mode_arg)
            {
                _vt_mode = true;
                continue;
            }
            if (token == inherit_cursor_arg)
            {
                _inherit_cursor = true;
                continue;
            }
            if (token == ambiguous_is_wide_arg)
            {
                _ambiguous_is_wide = true;
                continue;
            }

            // 只允许以下三个值：
            //   "graphemes" Unicode 字簇（grapheme cluster）测量宽�?
            //   "wcswidth" wcswidth 规则测量宽度
            //   "console" 传统 conhost 规则测量宽度
            if (token == text_measurement_arg)
            {
                using enum conpty::text_measurement_mode;
                if (parser.done())
                    throw parse_error::missing_text_measurement_value;
                const auto value = parser.next();
                if (value == text_graphemes)
                    _text_measurement = graphemes;
                else if (value == text_wcswidth)
                    _text_measurement = wcswidth;
                else if (value == text_console)
                    _text_measurement = console;
                else
                    throw parse_error::invalid_text_measurement_value;
                continue;
            }

            // ---- 显式客户端命令行分隔�?------------------------------------
            if (token == client_commandline_arg)
            {
                // 跳过 "--"
                _client_command_line = parser.remain();
                return;
            }

            // ---- 未知 token -> 隐式客户端负�?-------------------------------
            _client_command_line = remaining;
            return;
        }
    }

    // --- 查询接口 -------------------------------------------------------
    [[nodiscard]] bool is_headless() const noexcept
    {
        return _headless;
    }
    [[nodiscard]] std::uintptr_t server_handle() const noexcept
    {
        return _server_handle;
    }
    [[nodiscard]] std::uintptr_t condrv_handle() const noexcept
    {
        return _condrv_handle;
    }
    [[nodiscard]] bool com_server() const noexcept
    {
        return _run_as_com_server;
    }
    [[nodiscard]] std::uintptr_t signal_handle() const noexcept
    {
        return _signal_handle;
    }
    [[nodiscard]] const win32::wcstring_view client_command_line() const noexcept
    {
        return _client_command_line;
    }
    [[nodiscard]] const win32::wcstring_view original_command_line() const noexcept
    {
        return _original_command_line;
    }
    [[nodiscard]] conpty::text_measurement_mode text_measurement() const noexcept
    {
        return _text_measurement;
    }
    [[nodiscard]] bool vt_mode() const noexcept
    {
        return _vt_mode;
    }
    [[nodiscard]] bool force_v1() const noexcept
    {
        return _force_v1;
    }
    [[nodiscard]] bool force_no_handoff() const noexcept
    {
        return _force_no_handoff;
    }
    [[nodiscard]] short width() const noexcept
    {
        return _width;
    }
    [[nodiscard]] short height() const noexcept
    {
        return _height;
    }
    [[nodiscard]] bool inherit_cursor() const noexcept
    {
        return _inherit_cursor;
    }
    [[nodiscard]] bool ambiguous_is_wide() const noexcept
    {
        return _ambiguous_is_wide;
    }

  private:
    uintptr_t parse_handle_value(std::wstring_view text)
    {
        if (text.size() > 16)
            throw parse_error::invalid_handle_value;
        char buffer[16]{};
        std::ranges::copy(std::views::transform(text,
                                                [](wchar_t ch) {
                                                    if (ch > L'\x007F')
                                                        throw parse_error::invalid_handle_value;
                                                    return static_cast<char>(ch);
                                                }),
                          std::begin(buffer));
        uintptr_t value{};
        auto end_ptr = std::begin(buffer) + text.size();
        auto res = std::from_chars(std::begin(buffer), end_ptr, value, 16);
        if (res.ec != std::errc{})
            throw parse_error::invalid_handle_value;
        return value;
    }

    short parse_size_value(std::wstring_view text)
    {
        if (text.size() > 5)
            throw parse_error::invalid_size_value;
        char buffer[5]{};
        std::ranges::copy(std::views::transform(text,
                                                [](wchar_t ch) {
                                                    if (ch > L'\x007F')
                                                        throw parse_error::invalid_size_value;
                                                    return static_cast<char>(ch);
                                                }),
                          std::begin(buffer));
        short value{};
        auto end_ptr = std::begin(buffer) + text.size();
        auto res = std::from_chars(std::begin(buffer), end_ptr, value, 10);
        if (res.ec != std::errc{} || value < 0)
            throw parse_error::invalid_size_value;
        return value;
    }

    win32::wcstring_view _client_command_line;
    win32::wcstring_view _original_command_line;

    conpty::text_measurement_mode _text_measurement{};

    bool _force_no_handoff = false;
    bool _force_v1 = false;
    bool _vt_mode = false;
    bool _headless = false;
    bool _inherit_cursor = false;
    bool _ambiguous_is_wide = false;
    bool _run_as_com_server = false;
    short _width = 0;
    short _height = 0;

    uintptr_t _condrv_handle = 0;
    uintptr_t _server_handle = 0;
    uintptr_t _signal_handle = 0;
};

} // namespace console