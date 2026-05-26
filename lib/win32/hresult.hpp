// ── win32/hresult.hpp ────────────────────────────────────
// 强类型 HRESULT
//
// COM 函数返回值。替代 winerror.h 中散落的 FACILITY_*/SEVERITY_* 宏。

#pragma once
#include <cstdint>

namespace win32
{

enum class hresult : long
{
};

[[nodiscard]] inline bool failed(hresult hr) noexcept
{
    return FAILED(std::to_underlying(hr));
}

[[nodiscard]] inline bool succeeded(hresult hr) noexcept
{
    return !failed(hr);
}

inline void throw_hresult(hresult hr)
{
    if (failed(hr))
        throw hr;
}

} // namespace win32
