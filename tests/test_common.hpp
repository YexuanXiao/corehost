#include <iostream>
#include <cstring>

inline int tests_passed = 0;
inline int tests_failed = 0;

#define RUN_TEST(func, name_str)                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        std::wcout << L"  " << name_str << L"... ";                                                                    \
        if (func())                                                                                                    \
        {                                                                                                              \
            std::wcout << L"PASSED" << std::endl;                                                                      \
            tests_passed++;                                                                                            \
        }                                                                                                              \
        else                                                                                                           \
        {                                                                                                              \
            std::wcout << L"FAILED" << std::endl;                                                                      \
            tests_failed++;                                                                                            \
        }                                                                                                              \
    } while (0)

#define ASSERT(cond)                                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(cond))                                                                                                   \
        {                                                                                                              \
            std::wcerr << L"  ASSERTION FAILED: " << #cond << L" (" << __FILE__ << L":" << __LINE__ << L")"            \
                       << std::endl;                                                                                   \
            return false;                                                                                              \
        }                                                                                                              \
    } while (0)

#define ASSERT_EQ(a, b) ASSERT((a) == (b))
