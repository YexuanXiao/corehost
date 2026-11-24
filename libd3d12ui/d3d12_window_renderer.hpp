#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <dcomp.h>
#include <dxgi1_6.h>
#include <winrt/base.h>

#include <cstdint>

namespace corehost::d3d12ui
{

// Owns the top-level window's D3D12 device, composition swapchain and the
// minimal clear pass. Text/UI passes are added on top of this class.
class d3d12_window_renderer final
{
  public:
    explicit d3d12_window_renderer(HWND window);
    ~d3d12_window_renderer();

    d3d12_window_renderer(const d3d12_window_renderer &) = delete;
    d3d12_window_renderer &operator=(const d3d12_window_renderer &) = delete;

    void resize(UINT width, UINT height);
    void render();

  private:
    void create_device();
    void create_swapchain();
    void create_rtvs();
    void wait_for_gpu();

    HWND window_{};
    UINT width_{};
    UINT height_{};
    bool buffer_initialized_[2]{};

    winrt::com_ptr<ID3D12Device> device_;
    winrt::com_ptr<ID3D12CommandQueue> queue_;
    winrt::com_ptr<IDXGISwapChain3> swapchain_;
    winrt::com_ptr<IDCompositionDevice> composition_device_;
    winrt::com_ptr<IDCompositionTarget> composition_target_;
    winrt::com_ptr<IDCompositionVisual> composition_visual_;
    winrt::com_ptr<ID3D12DescriptorHeap> rtv_heap_;
    winrt::com_ptr<ID3D12CommandAllocator> allocator_;
    winrt::com_ptr<ID3D12GraphicsCommandList> command_list_;
    winrt::com_ptr<ID3D12Fence> fence_;
    winrt::com_ptr<ID3D12Resource> backbuffers_[2];
    HANDLE fence_event_{};
    std::uint64_t fence_value_{};
    UINT rtv_size_{};
};

} // namespace corehost::d3d12ui
