#pragma once

#ifndef COREHOST_LOG_LEVEL
#define COREHOST_LOG_LEVEL 1
#endif

#if COREHOST_LOG_LEVEL < 0 || COREHOST_LOG_LEVEL > 3
#error COREHOST_LOG_LEVEL must be between 0 and 3
#endif

// 日志等级按触发频率划分，而不是严重程度：
// LOG/LOG_HEX 是低频，LOG2/LOG2_HEX 是中频，LOG3/LOG3_HEX 是高频。
// COREHOST_LOG_LEVEL 在编译期决定启用到哪一级；被禁用的宏不会求值参数。
#ifdef COREHOST_DISABLE_LOG
#define LOG(fmt, ...) ((void)0)
#define LOG2(fmt, ...) ((void)0)
#define LOG3(fmt, ...) ((void)0)
#define LOG_IF(condition, fmt, ...) ((void)0)
#define LOG2_IF(condition, fmt, ...) ((void)0)
#define LOG3_IF(condition, fmt, ...) ((void)0)
#define LOG_HEX(tag, data, size) ((void)0)
#define LOG2_HEX(tag, data, size) ((void)0)
#define LOG3_HEX(tag, data, size) ((void)0)
#define LOG_HEX_IF(condition, tag, data, size) ((void)0)
#define LOG2_HEX_IF(condition, tag, data, size) ((void)0)
#define LOG3_HEX_IF(condition, tag, data, size) ((void)0)
#else

#include <cstddef>

void core_log(const wchar_t *fmt, ...) noexcept;
void core_log_hex(const char *function_name, const char *tag, const void *data, std::size_t size) noexcept;

#if COREHOST_LOG_LEVEL >= 1
#define LOG(fmt, ...) core_log(L"%-35hs " fmt L"\n", __func__, ##__VA_ARGS__)
#define LOG_IF(condition, fmt, ...)                                                                                    \
    do                                                                                                                 \
    {                                                                                                                  \
        if (condition)                                                                                                 \
            LOG(fmt, ##__VA_ARGS__);                                                                                   \
    } while (false)
#define LOG_HEX(tag, data, size) core_log_hex(__func__, tag, data, size)
#define LOG_HEX_IF(condition, tag, data, size)                                                                         \
    do                                                                                                                 \
    {                                                                                                                  \
        if (condition)                                                                                                 \
            LOG_HEX(tag, data, size);                                                                                  \
    } while (false)
#else
#define LOG(fmt, ...) ((void)0)
#define LOG_IF(condition, fmt, ...) ((void)0)
#define LOG_HEX(tag, data, size) ((void)0)
#define LOG_HEX_IF(condition, tag, data, size) ((void)0)
#endif

#if COREHOST_LOG_LEVEL >= 2
#define LOG2(fmt, ...) core_log(L"%-35hs " fmt L"\n", __func__, ##__VA_ARGS__)
#define LOG2_IF(condition, fmt, ...)                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        if (condition)                                                                                                 \
            LOG2(fmt, ##__VA_ARGS__);                                                                                  \
    } while (false)
#define LOG2_HEX(tag, data, size) core_log_hex(__func__, tag, data, size)
#define LOG2_HEX_IF(condition, tag, data, size)                                                                        \
    do                                                                                                                 \
    {                                                                                                                  \
        if (condition)                                                                                                 \
            LOG2_HEX(tag, data, size);                                                                                 \
    } while (false)
#else
#define LOG2(fmt, ...) ((void)0)
#define LOG2_IF(condition, fmt, ...) ((void)0)
#define LOG2_HEX(tag, data, size) ((void)0)
#define LOG2_HEX_IF(condition, tag, data, size) ((void)0)
#endif

#if COREHOST_LOG_LEVEL >= 3
#define LOG3(fmt, ...) core_log(L"%-35hs " fmt L"\n", __func__, ##__VA_ARGS__)
#define LOG3_IF(condition, fmt, ...)                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        if (condition)                                                                                                 \
            LOG3(fmt, ##__VA_ARGS__);                                                                                  \
    } while (false)
#define LOG3_HEX(tag, data, size) core_log_hex(__func__, tag, data, size)
#define LOG3_HEX_IF(condition, tag, data, size)                                                                        \
    do                                                                                                                 \
    {                                                                                                                  \
        if (condition)                                                                                                 \
            LOG3_HEX(tag, data, size);                                                                                 \
    } while (false)
#else
#define LOG3(fmt, ...) ((void)0)
#define LOG3_IF(condition, fmt, ...) ((void)0)
#define LOG3_HEX(tag, data, size) ((void)0)
#define LOG3_HEX_IF(condition, tag, data, size) ((void)0)
#endif

#endif
