
#include <string>

namespace shell
{
std::wstring get_shell_path();
std::wstring get_system_conhost_path();
bool file_exists(PCWSTR file_path);
} // namespace shell