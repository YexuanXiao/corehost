#pragma once

#include "d3d12_window_renderer.hpp"

#include <Windows.h>

#include <memory>
#include <string>

namespace corehost::window
{

// Top-level Win32 host for window mode. It owns the HWND and the D3D12
// composition renderer; higher-level console UI is added above it.
class window_host final
{
  public:
    explicit window_host(std::wstring title);
    ~window_host();

    [[nodiscard]] int run();

  private:
    static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

    std::wstring title_;
    std::unique_ptr<d3d12ui::d3d12_window_renderer> renderer_;
};

} // namespace corehost::window
