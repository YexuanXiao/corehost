#pragma once

// SGR/text-effect cases shared by the worker and the parent-side test driver.

#include <array>
#include <string>
#include <string_view>

namespace bench
{

struct sgr_sequence_case
{
    std::string_view name;
    std::string_view prefix;
    std::string_view suffix;
};

inline constexpr std::string_view sgr_sequence_payload =
    "ASCII abcdefghijklmnopqrstuvwxyz 0123456789 喜欢你 核心终端性能测试 VT-SGR payload payload payload";

inline constexpr std::array<sgr_sequence_case, 20> sgr_sequence_cases{{
    {"plain", "", ""},
    {"reset", "\x1b[0m", ""},
    {"bold", "\x1b[1m", "\x1b[22m"},
    {"faint", "\x1b[2m", "\x1b[22m"},
    {"italic", "\x1b[3m", "\x1b[23m"},
    {"underline", "\x1b[4m", "\x1b[24m"},
    {"blink", "\x1b[5m", "\x1b[25m"},
    {"inverse", "\x1b[7m", "\x1b[27m"},
    {"hidden", "\x1b[8m", "\x1b[28m"},
    {"strike", "\x1b[9m", "\x1b[29m"},
    {"fg-16", "\x1b[31m", "\x1b[39m"},
    {"fg-bright", "\x1b[93m", "\x1b[39m"},
    {"bg-16", "\x1b[44m", "\x1b[49m"},
    {"bg-bright", "\x1b[102m", "\x1b[49m"},
    {"fg-256", "\x1b[38;5;202m", "\x1b[39m"},
    {"bg-256", "\x1b[48;5;24m", "\x1b[49m"},
    {"fg-rgb", "\x1b[38;2;12;120;220m", "\x1b[39m"},
    {"bg-rgb", "\x1b[48;2;32;40;48m", "\x1b[49m"},
    {"combined", "\x1b[1;3;4;38;2;220;80;40m", "\x1b[0m"},
    {"combined-bg", "\x1b[7;48;2;32;40;48;38;5;202m", "\x1b[0m"},
}};

inline const sgr_sequence_case *find_sgr_sequence_case(std::string_view name) noexcept
{
    for (const auto &test_case : sgr_sequence_cases)
        if (test_case.name == name)
            return &test_case;
    return nullptr;
}

inline std::string make_sgr_sequence_line(const sgr_sequence_case &test_case)
{
    std::string line;
    line.reserve(test_case.prefix.size() + sgr_sequence_payload.size() + test_case.suffix.size() + 2);
    line.append(test_case.prefix);
    line.append(sgr_sequence_payload);
    line.append(test_case.suffix);
    line.append("\r\n");
    return line;
}

} // namespace bench
