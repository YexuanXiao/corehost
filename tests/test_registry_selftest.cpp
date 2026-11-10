// tests/test_registry_selftest.cpp
// 注册表守卫自测：原值不存在/存在两种情况都必须精确恢复。
#include "lib/registry.hpp"
#include "lib/runner.hpp"

#include <cstdio>
#include <cstring>

namespace
{

std::wstring probe_key()
{
    return L"Software\\corehost-test\\registry-guard-" + std::to_wstring(::GetCurrentProcessId());
}

corehost::test::result restores_absent_value()
{
    const auto key = probe_key();
    DWORD error = 0;
    REQUIRE(corehost::test::delete_registry_tree(HKEY_CURRENT_USER, key, error));

    {
        corehost::test::registry_value_guard guard{HKEY_CURRENT_USER, key, L"Probe"};
        REQUIRE(guard.captured());
        REQUIRE(!guard.snapshot().existed);

        REQUIRE(corehost::test::write_registry_dword(HKEY_CURRENT_USER, key, L"Probe", 42, error));
        REQUIRE(guard.restore());
    }

    // 恢复后值必须重新不存在。
    corehost::test::registry_snapshot after{};
    REQUIRE(corehost::test::read_registry_value(HKEY_CURRENT_USER, key, L"Probe", after, error));
    REQUIRE(!after.existed);
    REQUIRE(corehost::test::delete_registry_tree(HKEY_CURRENT_USER, key, error));
    return corehost::test::result::pass;
}

corehost::test::result restores_existing_value()
{
    const auto key = probe_key();
    DWORD error = 0;
    REQUIRE(corehost::test::delete_registry_tree(HKEY_CURRENT_USER, key, error));
    REQUIRE(corehost::test::write_registry_dword(HKEY_CURRENT_USER, key, L"Probe", 7, error));

    {
        corehost::test::registry_value_guard guard{HKEY_CURRENT_USER, key, L"Probe"};
        REQUIRE(guard.captured());
        REQUIRE(guard.snapshot().existed);

        REQUIRE(corehost::test::write_registry_dword(HKEY_CURRENT_USER, key, L"Probe", 9, error));
        REQUIRE(guard.restore());
    }

    corehost::test::registry_snapshot after{};
    REQUIRE(corehost::test::read_registry_value(HKEY_CURRENT_USER, key, L"Probe", after, error));
    REQUIRE(after.existed);
    REQUIRE(after.type == REG_DWORD);
    REQUIRE(after.data.size() == sizeof(DWORD));
    DWORD value = 0;
    std::memcpy(&value, after.data.data(), sizeof(value));
    REQUIRE(value == 7);

    REQUIRE(corehost::test::delete_registry_tree(HKEY_CURRENT_USER, key, error));
    return corehost::test::result::pass;
}

} // namespace

int main()
{
    constexpr corehost::test::test_case cases[] = {
        {"absent", "registry", &restores_absent_value},
        {"existing", "registry", &restores_existing_value},
    };

    if (const auto child = corehost::test::run_child_case(cases))
        return *child;

    std::fwprintf(stdout, L"=== registry self test ===\n");
    const auto stats = corehost::test::run_isolated(cases);
    std::fwprintf(stdout, L"  %d passed, %d failed, %d skipped\n", stats.passed, stats.failed, stats.skipped);
    return corehost::test::exit_code(stats);
}
