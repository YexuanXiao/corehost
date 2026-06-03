#include <windows.h>
#include <objbase.h>
#include <stdexcept>
#include "com_ptr.hpp"
namespace com
{
template <typename T>
// exe 注册 COM 服务器用这种方式
class register_object
{
  public:
    // 构造函数：注册类对象
    register_object(REFCLSID clsid, com::com_ptr<T> &pUnk, DWORD dwClsContext = CLSCTX_LOCAL_SERVER,
                    DWORD dwFlags = REGCLS_SINGLEUSE)
        : m_cookie(0)
    {
        HRESULT hr = CoRegisterClassObject(clsid, pUnk.get(), dwClsContext, dwFlags, &m_cookie);
        win32::throw_hresult(win32::hresult(hr));
    }

    // 析构函数：撤销注册
    ~register_object()
    {
        if (m_cookie != 0)
        {
            (void)::CoRevokeClassObject(m_cookie);
        }
    }

    register_object(const register_object &) = delete;
    register_object &operator=(const register_object &) = delete;

  private:
    DWORD m_cookie;
};
} // namespace com