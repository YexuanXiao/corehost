// tests/lib/registry.hpp
// 注册表快照/恢复守卫。
//
// defterm 用例需要临时修改 HKCU\Console\%%Startup\DelegationConsole 等用户设置，
// 任何失败路径都必须恢复原值，否则会污染用户环境（见 doc/design/0003）。
// 这里提供最小的读/写/删除与 RAII 守卫，错误通过返回值报告（测试层不用异常）。
#pragma once
#include <windows.h>

#include <string>
#include <string_view>
#include <vector>

namespace corehost::test
{

struct registry_snapshot
{
    bool existed = false;
    DWORD type = 0;
    std::vector<std::byte> data;
};

inline bool read_registry_value(HKEY root, std::wstring_view subkey, std::wstring_view name,
                                registry_snapshot &snapshot, DWORD &error)
{
    error = 0;
    snapshot = {};

    HKEY key = nullptr;
    LSTATUS status = ::RegOpenKeyExW(root, std::wstring{subkey}.c_str(), 0, KEY_QUERY_VALUE, &key);
    if (status == ERROR_FILE_NOT_FOUND)
        return true; // 键不存在 == 值不存在
    if (status != ERROR_SUCCESS)
    {
        error = static_cast<DWORD>(status);
        return false;
    }

    DWORD type = 0;
    DWORD size = 0;
    status = ::RegQueryValueExW(key, std::wstring{name}.c_str(), nullptr, &type, nullptr, &size);
    if (status == ERROR_FILE_NOT_FOUND)
    {
        ::RegCloseKey(key);
        return true;
    }
    if (status != ERROR_SUCCESS)
    {
        error = static_cast<DWORD>(status);
        ::RegCloseKey(key);
        return false;
    }

    snapshot.existed = true;
    snapshot.type = type;
    snapshot.data.resize(size);
    if (size != 0)
    {
        status = ::RegQueryValueExW(key, std::wstring{name}.c_str(), nullptr, &type,
                                    reinterpret_cast<LPBYTE>(snapshot.data.data()), &size);
        if (status != ERROR_SUCCESS)
        {
            error = static_cast<DWORD>(status);
            ::RegCloseKey(key);
            return false;
        }
        snapshot.data.resize(size);
    }

    ::RegCloseKey(key);
    return true;
}

inline bool write_registry_value(HKEY root, std::wstring_view subkey, std::wstring_view name, DWORD type,
                                 const void *data, DWORD size, DWORD &error)
{
    error = 0;
    HKEY key = nullptr;
    LSTATUS status = ::RegCreateKeyExW(root, std::wstring{subkey}.c_str(), 0, nullptr, 0,
                                       KEY_SET_VALUE | KEY_CREATE_SUB_KEY | KEY_QUERY_VALUE, nullptr, &key, nullptr);
    if (status != ERROR_SUCCESS)
    {
        error = static_cast<DWORD>(status);
        return false;
    }

    status = ::RegSetValueExW(key, std::wstring{name}.c_str(), 0, type, static_cast<const BYTE *>(data), size);
    ::RegCloseKey(key);
    if (status != ERROR_SUCCESS)
    {
        error = static_cast<DWORD>(status);
        return false;
    }
    return true;
}

inline bool write_registry_string(HKEY root, std::wstring_view subkey, std::wstring_view name, std::wstring_view value,
                                  DWORD &error)
{
    const DWORD bytes = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
    return write_registry_value(root, subkey, name, REG_SZ, std::wstring{value}.c_str(), bytes, error);
}

inline bool write_registry_dword(HKEY root, std::wstring_view subkey, std::wstring_view name, DWORD value, DWORD &error)
{
    return write_registry_value(root, subkey, name, REG_DWORD, &value, sizeof(value), error);
}

// 恢复快照：原来不存在则删除值，存在则写回原类型/数据。
inline bool restore_registry_value(HKEY root, std::wstring_view subkey, std::wstring_view name,
                                   const registry_snapshot &snapshot, DWORD &error)
{
    if (snapshot.existed)
        return write_registry_value(root, subkey, name, snapshot.type, snapshot.data.data(),
                                    static_cast<DWORD>(snapshot.data.size()), error);

    error = 0;
    HKEY key = nullptr;
    LSTATUS status = ::RegOpenKeyExW(root, std::wstring{subkey}.c_str(), 0, KEY_SET_VALUE, &key);
    if (status == ERROR_FILE_NOT_FOUND)
        return true;
    if (status != ERROR_SUCCESS)
    {
        error = static_cast<DWORD>(status);
        return false;
    }

    status = ::RegDeleteValueW(key, std::wstring{name}.c_str());
    ::RegCloseKey(key);
    if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND)
    {
        error = static_cast<DWORD>(status);
        return false;
    }
    return true;
}

// 删除整棵测试用的键（用例自建的临时键，不删除用户真实键）。
inline bool delete_registry_tree(HKEY root, std::wstring_view subkey, DWORD &error)
{
    error = 0;
    const LSTATUS status = ::RegDeleteTreeW(root, std::wstring{subkey}.c_str());
    if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND)
    {
        error = static_cast<DWORD>(status);
        return false;
    }
    return true;
}

// RAII：构造时快照，析构时恢复。restore() 返回 false 时调用方应让用例失败
// （例如在用例结束前显式调用并断言）。
class registry_value_guard
{
  public:
    registry_value_guard(HKEY root, std::wstring subkey, std::wstring name)
        : _root{root}, _subkey{std::move(subkey)}, _name{std::move(name)}
    {
        _captured = read_registry_value(_root, _subkey, _name, _snapshot, _error);
    }

    registry_value_guard(const registry_value_guard &) = delete;
    registry_value_guard &operator=(const registry_value_guard &) = delete;

    ~registry_value_guard()
    {
        (void)restore();
    }

    [[nodiscard]] bool captured() const noexcept
    {
        return _captured;
    }

    [[nodiscard]] DWORD error() const noexcept
    {
        return _error;
    }

    [[nodiscard]] const registry_snapshot &snapshot() const noexcept
    {
        return _snapshot;
    }

    // 幂等：成功恢复后再次调用返回 true。
    bool restore() noexcept
    {
        if (!_captured || _restored)
            return _captured;
        _restored = restore_registry_value(_root, _subkey, _name, _snapshot, _error);
        return _restored;
    }

  private:
    HKEY _root;
    std::wstring _subkey;
    std::wstring _name;
    registry_snapshot _snapshot;
    DWORD _error = 0;
    bool _captured = false;
    bool _restored = false;
};

} // namespace corehost::test
