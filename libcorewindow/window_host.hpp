#pragma once

#include <Windows.h>

#include <string>

namespace corehost::window
{

// Minimal top-level Win32 host for window mode. Rendering and console session
// integration are layered on top of this host in later milestones.
class window_host final
{
  public:
    explicit window_host(std::wstring title);

    [[nodiscard]] int run();

  private:
    static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

    std::wstring title_;
};

} // namespace corehost::window
