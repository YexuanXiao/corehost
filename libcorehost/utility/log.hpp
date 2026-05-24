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
#include <cstdarg>
#include <cstdlib> // for std::abort
#include <string>
#include <vector>
#include <io.h>    // for _fileno, _get_osfhandle
#include <fcntl.h> // for _O_* constants (if needed)

// 单独的日志文件初始化函数（原 lambda 提取至此）
inline FILE *init_log_file()
{
    // ---------- 1. 获取模块所在目录（两步法，不用 MAX_PATH） ----------
    std::wstring exe_dir;
    {
        DWORD size = 512;               // 初始探测大小，拒绝写死 MAX_PATH
        std::vector<wchar_t> buf(size); // ← 修正：原来是 <<wchar_t>
        while (true)
        {
            DWORD len = ::GetModuleFileNameW(nullptr, buf.data(), size);
            if (len == 0)
                std::abort(); // 获取模块路径失败

            // len < size 表示缓冲区足够（含 null 终止位）
            if (len < size)
            {
                exe_dir.assign(buf.data(), len);
                break;
            }

            // 缓冲区不足，倍增后重试
            size *= 2;
            buf.resize(size);
        }

        // 截断到目录部分
        auto pos = exe_dir.find_last_of(L"\\/");
        if (pos != std::wstring::npos)
            exe_dir.resize(pos + 1);
        else
            exe_dir = L".\\";
    }

    // ---------- 2. 选择日志根目录 ----------
    std::wstring base_path;
    if (exe_dir.find(L"system32") != std::wstring::npos)
    {
        // 严格两步法获取 %TEMP%：第一步先获得大小，第二步再分配缓冲区
        DWORD needed = ::GetEnvironmentVariableW(L"TEMP", nullptr, 0);
        if (needed == 0)
        {
            // 回退到 GetTempPathW，同样两步法
            needed = ::GetTempPathW(0, nullptr);
            if (needed == 0)
                std::abort();

            std::wstring temp(needed, L'\0');
            DWORD actual = ::GetTempPathW(needed, temp.data());
            if (actual == 0 || actual > needed)
                std::abort();
            temp.resize(actual);
            base_path = std::move(temp);
        }
        else
        {
            std::wstring temp(needed, L'\0');
            DWORD actual = ::GetEnvironmentVariableW(L"TEMP", temp.data(), needed);
            if (actual == 0 || actual >= needed)
                std::abort();
            temp.resize(actual);
            base_path = std::move(temp);
        }
    }
    else
    {
        base_path = std::move(exe_dir);
    }

    // 确保根目录末尾有分隔符
    if (!base_path.empty() && base_path.back() != L'\\' && base_path.back() != L'/')
        base_path.push_back(L'\\');

    // ---------- 3. 自动创建 logs 文件夹 ----------
    std::wstring logs_dir = base_path + L"logs";
    if (!::CreateDirectoryW(logs_dir.c_str(), nullptr) && ::GetLastError() != ERROR_ALREADY_EXISTS)
        std::abort(); // 创建失败且不是因为目录已存在

    logs_dir.push_back(L'\\');

    // ---------- 4. 构造日志文件名 ----------
    std::time_t now = std::time(nullptr);
    std::tm tm_buf;
    localtime_s(&tm_buf, &now);
    wchar_t time_str[32];
    std::wcsftime(time_str, 32, L"%Y%m%d_%H%M%S", &tm_buf);

    DWORD pid = ::GetCurrentProcessId();

    wchar_t filename[64];
    std::swprintf(filename, std::size(filename), L"corehost_%u_%ls.log", pid, time_str);

    std::wstring full_path = logs_dir + filename;

    // ---------- 5. 以追加模式打开，允许其他进程同时读取 ----------
    FILE *f = ::_wfsopen(full_path.c_str(), L"a", _SH_DENYNO);
    if (!f)
        std::abort();

    return f;
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
    std::fputws(buf, g_log_file);
    std::fflush(g_log_file);
}

#define LOG(fmt, ...) core_log(L"%-35s " fmt L"\n", __FUNCTIONW__, ##__VA_ARGS__)
#endif