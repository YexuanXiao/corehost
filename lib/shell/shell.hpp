
#include <string>

namespace shell
{
struct get_shell_result
{
    win32::wcstring_view name;
    std::wstring path;
};

get_shell_result get_shell();
std::wstring get_system_conhost_path();
bool file_exists(PCWSTR file_path);
std::wstring get_module_path();
std::wstring get_module_dir_path();
} // namespace shell