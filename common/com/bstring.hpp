#include <windows.h>
#include <oleauto.h>

namespace com
{
class bstring
{
  public:
    // 构造函数：从宽字符串构造 BSTR，允许空指针（表示空字符串）
    explicit bstring(const wchar_t *str)
    {
        m_str = SysAllocString(str);
        if (m_str == nullptr)
        {
            std::abort();
        }
    }

    // 析构函数：释放 BSTR 资源
    ~bstring()
    {
        if (m_str != nullptr)
        {
            SysFreeString(m_str);
        }
    }

    // 禁用复制操作
    bstring(const bstring &) = delete;
    bstring &operator=(const bstring &) = delete;

    // 禁用移动操作
    bstring(bstring &&) = delete;
    bstring &operator=(bstring &&) = delete;

    BSTR get() const
    {
        return m_str;
    }

  private:
    BSTR m_str{};
};
} // namespace com
