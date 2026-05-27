#pragma once

// 日志宏，自动添加函数名
// 定义 COREHOST_DISABLE_LOG 可完全禁用日志（零开销），适用于 Release 性能关键路径
#ifdef COREHOST_DISABLE_LOG
#define LOG(fmt, ...) ((void)0)
#else
#include <windows.h>
#include <cstdio>
#include <ctime>
#include <cwchar>
#include <cwctype>
#include <cstdarg>
#include <string>
#include <vector>
#include <algorithm>
#include <io.h>    // for _fileno, _get_osfhandle
#include <fcntl.h> // for _O_* constants (if needed)

inline std::wstring lower_path(std::wstring path)
{
    std::ranges::replace(path, L'/', L'\\');
    std::ranges::transform(path, path.begin(), [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    return path;
}

inline std::wstring module_directory()
{
    DWORD size = 512;
    std::vector<wchar_t> buf(size);
    for (;;)
    {
        DWORD len = ::GetModuleFileNameW(nullptr, buf.data(), size);
        if (len == 0)
            return {};

        if (len < size)
        {
            std::wstring dir(buf.data(), len);
            auto pos = dir.find_last_of(L"\\/");
            if (pos != std::wstring::npos)
                dir.resize(pos + 1);
            else
                dir = L".\\";
            return dir;
        }

        size *= 2;
        buf.resize(size);
    }
}

inline std::wstring temp_directory()
{
    DWORD needed = ::GetEnvironmentVariableW(L"TEMP", nullptr, 0);
    if (needed != 0)
    {
        std::wstring temp(needed, L'\0');
        DWORD actual = ::GetEnvironmentVariableW(L"TEMP", temp.data(), needed);
        if (actual != 0 && actual < needed)
        {
            temp.resize(actual);
            return temp;
        }
    }

    DWORD size = 512;
    std::vector<wchar_t> buf(size);
    for (;;)
    {
        DWORD len = ::GetTempPathW(size, buf.data());
        if (len == 0)
            return {};
        if (len < size)
            return std::wstring(buf.data(), len);
        size = len + 1;
        buf.resize(size);
    }
}

inline bool is_system_directory(std::wstring_view dir)
{
    auto module_dir = lower_path(std::wstring(dir));

    DWORD size = 512;
    std::vector<wchar_t> buf(size);
    for (;;)
    {
        UINT len = ::GetSystemDirectoryW(buf.data(), size);
        if (len == 0)
            return module_dir.find(L"\\system32\\") != std::wstring::npos;
        if (len < size)
        {
            std::wstring system_dir(buf.data(), len);
            if (!system_dir.empty() && system_dir.back() != L'\\' && system_dir.back() != L'/')
                system_dir.push_back(L'\\');
            return module_dir == lower_path(std::move(system_dir));
        }
        size = len + 1;
        buf.resize(size);
    }
}

inline FILE *open_log_file(std::wstring base_path, const wchar_t *filename)
{
    if (base_path.empty())
        return nullptr;

    if (base_path.back() != L'\\' && base_path.back() != L'/')
        base_path.push_back(L'\\');

    std::wstring logs_dir = base_path + L"logs";
    if (!::CreateDirectoryW(logs_dir.c_str(), nullptr) && ::GetLastError() != ERROR_ALREADY_EXISTS)
        return nullptr;

    logs_dir.push_back(L'\\');
    return ::_wfsopen((logs_dir + filename).c_str(), L"a", _SH_DENYNO);
}

// 单独的日志文件初始化函数（原 lambda 提取至此）
inline FILE *init_log_file()
try
{
    auto exe_dir = module_directory();

    std::time_t now = std::time(nullptr);
    std::tm tm_buf{};
    if (localtime_s(&tm_buf, &now) != 0)
        return nullptr;
    wchar_t time_str[32]{};
    std::wcsftime(time_str, std::size(time_str), L"%Y%m%d_%H%M%S", &tm_buf);

    wchar_t filename[64]{};
    std::swprintf(filename, std::size(filename), L"corehost_%u_%ls.log", ::GetCurrentProcessId(), time_str);

    if (is_system_directory(exe_dir))
    {
        if (auto *f = open_log_file(temp_directory(), filename))
            return f;
    }
    else
    {
        if (auto *f = open_log_file(exe_dir, filename))
            return f;
    }

    return open_log_file(temp_directory(), filename);
}
catch (...)
{
    return nullptr;
}

// 内联全局文件指针，调用单独的初始化函数
inline FILE *g_log_file = init_log_file();

// 核心日志函数：直接写入已打开的文件，不检查文件指针有效性（假设已成功初始化）
inline void core_log(const wchar_t *fmt, ...)
{
    wchar_t buf[1024];
    auto now = std::time(nullptr);
    auto tm = std::localtime(&now);
    int off = std::swprintf(buf, std::size(buf), L"[%02d:%02d:%02d] ", tm->tm_hour, tm->tm_min, tm->tm_sec);
    va_list va;
    va_start(va, fmt);
    std::vswprintf(buf + off, std::size(buf) - off, fmt, va);
    va_end(va);

    // 写入并立即刷新，确保日志不丢失
    if (!g_log_file)
    {
        ::OutputDebugStringW(buf);
        return;
    }
    std::fputws(buf, g_log_file);
    std::fflush(g_log_file);
}

#define LOG(fmt, ...) core_log(L"%-35s " fmt L"\n", __FUNCTIONW__, ##__VA_ARGS__)
#endif
