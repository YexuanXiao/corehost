#pragma once

#include "common.hpp"

#include <cstdio>
#include <filesystem>

namespace bench
{

inline constexpr wchar_t powershell_type_input_file_name[] = L"corehost-bench-realistic-type-vt.txt";

inline void write_powershell_type_input_file(const std::filesystem::path &path)
{
    FILE *file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"wb") != 0 || !file)
        print_and_abort("create PowerShell type input file failed\n");

    constexpr size_t line_count = 12000;
    for (size_t i = 0; i < line_count; ++i)
    {
        const char *prefix = "\x1b[32m[build]\x1b[0m ";
        if (i % 17 == 0)
            prefix = "\x1b[33mwarning\x1b[0m ";
        else if (i % 53 == 0)
            prefix = "\x1b[31merror\x1b[0m ";
        else if (i % 7 == 0)
            prefix = "\x1b[36mcompile\x1b[0m ";

        if (std::fprintf(file,
                         "%sproject\\module\\target_%05zu.cpp : ASCII abcdefghijklmnopqrstuvwxyz 0123456789 "
                         "\x1b[1m喜欢你 核心终端性能测试\x1b[22m payload payload payload payload\r\n",
                         prefix, i) < 0)
        {
            std::fclose(file);
            print_and_abort("write PowerShell type input file failed\n");
        }
    }

    if (std::fclose(file) != 0)
        print_and_abort("close PowerShell type input file failed\n");
}

} // namespace bench
