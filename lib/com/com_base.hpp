// ── com/com_base.hpp ─────────────────────────────────────
// COM 对象基类 — 最简 CRTP 实现，替代 winrt::implements。
//
// 提供线程安全的 IUnknown (QueryInterface/AddRef/Release)。
// 派生类只需实现业务接口方法。
//
// 用法:
//   struct my_obj : com::implements<my_obj, IFoo, IBar>
//   {
//       // IFoo/IBar 方法 ...
//   };

#pragma once
#include <windows.h>
#include <objbase.h>

namespace com
{

template <typename Derived, typename... Interfaces>
struct implements : public Interfaces...
{
    LONG ref_count{1};

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **obj) noexcept
    {
        if (!obj)
            return E_INVALIDARG;
        *obj = nullptr;

        // IUnknown 永远返回第一个接口（保证 COM 身份一致性）
        if (riid == __uuidof(IUnknown))
        {
            using First = std::tuple_element_t<0, std::tuple<Interfaces...>>;
            *obj = static_cast<First *>(static_cast<Derived *>(this));
            AddRef();
            return S_OK;
        }

        return _query_impl<Interfaces...>(riid, obj);
    }

    ULONG STDMETHODCALLTYPE AddRef() noexcept
    {
        return static_cast<ULONG>(::InterlockedIncrement(&ref_count));
    }

    ULONG STDMETHODCALLTYPE Release() noexcept
    {
        auto rc = ::InterlockedDecrement(&ref_count);
        if (rc == 0)
            delete static_cast<Derived *>(this);
        return static_cast<ULONG>(rc);
    }

  private:
    template <typename First, typename... Rest>
    HRESULT _query_impl(REFIID riid, void **obj) noexcept
    {
        if (riid == __uuidof(First))
        {
            *obj = static_cast<First *>(static_cast<Derived *>(this));
            AddRef();
            return S_OK;
        }
        if constexpr (sizeof...(Rest) > 0)
            return _query_impl<Rest...>(riid, obj);
        return E_NOINTERFACE;
    }
};

} // namespace com
