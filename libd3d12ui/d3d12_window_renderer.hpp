#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <dcomp.h>
#include <dxgi1_6.h>
#include <winrt/base.h>
#include "win32/event.hpp"

#include <cassert>
#include <cstdint>

namespace corehost::d3d12ui
{

// Header-only owner of the top-level window's D3D12 device and composition
// swapchain. The renderer is stored directly in the window host; it is never
// heap allocated.
class d3d12_window_renderer final
{
  public:
    d3d12_window_renderer() = default;

    explicit d3d12_window_renderer(HWND window)
    {
        initialize(window);
    }

    ~d3d12_window_renderer()
    {
        reset();
    }

    d3d12_window_renderer(const d3d12_window_renderer &) = delete;
    d3d12_window_renderer &operator=(const d3d12_window_renderer &) = delete;

    void initialize(HWND window)
    {
        assert(window != nullptr);
        assert(!initialized_);

        window_ = window;
        fence_event_ = win32::event{win32::create_tag, false, false};

        RECT client{};
        if (::GetClientRect(window_, &client) == FALSE)
        {
            winrt::throw_last_error();
        }
        width_ = static_cast<UINT>(client.right - client.left);
        height_ = static_cast<UINT>(client.bottom - client.top);

        create_device();
        create_swapchain();
        create_rtvs();
        initialized_ = true;
    }

    void reset() noexcept
    {
        initialized_ = false;
        backbuffers_[0] = {};
        backbuffers_[1] = {};
        command_list_ = {};
        allocator_ = {};
        fence_ = {};
        rtv_heap_ = {};
        composition_visual_ = {};
        composition_target_ = {};
        composition_device_ = {};
        swapchain_ = {};
        queue_ = {};
        device_ = {};
        fence_event_.clear();
        width_ = 0;
        height_ = 0;
    }

    [[nodiscard]] bool initialized() const noexcept
    {
        return initialized_;
    }

    void resize(UINT width, UINT height)
    {
        if (!initialized_ || width == 0 || height == 0 || (width == width_ && height == height_))
        {
            return;
        }

        width_ = width;
        height_ = height;
        wait_for_gpu();
        backbuffers_[0] = {};
        backbuffers_[1] = {};
        winrt::check_hresult(swapchain_->ResizeBuffers(0, width_, height_, DXGI_FORMAT_UNKNOWN, 0));
        create_rtvs();
        render();
    }

    void render()
    {
        if (!initialized_)
        {
            return;
        }

        winrt::check_hresult(allocator_->Reset());
        winrt::check_hresult(command_list_->Reset(allocator_.get(), nullptr));

        const auto index = swapchain_->GetCurrentBackBufferIndex();
        auto &buffer = backbuffers_[index];

        D3D12_RESOURCE_BARRIER to_target{};
        to_target.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        to_target.Transition.pResource = buffer.get();
        to_target.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        to_target.Transition.StateBefore =
            buffer_initialized_[index] ? D3D12_RESOURCE_STATE_PRESENT : D3D12_RESOURCE_STATE_COMMON;
        to_target.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        command_list_->ResourceBarrier(1, &to_target);

        auto rtv = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
        rtv.ptr += static_cast<SIZE_T>(index) * rtv_size_;
        constexpr float clear_color[4] = {0.015f, 0.015f, 0.02f, 1.0f};
        command_list_->ClearRenderTargetView(rtv, clear_color, 0, nullptr);

        D3D12_RESOURCE_BARRIER to_present = to_target;
        to_present.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        to_present.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        command_list_->ResourceBarrier(1, &to_present);

        winrt::check_hresult(command_list_->Close());
        ID3D12CommandList *lists[] = {command_list_.get()};
        queue_->ExecuteCommandLists(1, lists);
        winrt::check_hresult(swapchain_->Present(1, 0));
        buffer_initialized_[index] = true;
    }

  private:
    void create_device()
    {
        winrt::check_hresult(::D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(device_.put())));

        D3D12_COMMAND_QUEUE_DESC queue_desc{};
        queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        winrt::check_hresult(device_->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(queue_.put())));

        winrt::check_hresult(
            device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(allocator_.put())));
        winrt::check_hresult(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator_.get(), nullptr,
                                                        IID_PPV_ARGS(command_list_.put())));
        winrt::check_hresult(command_list_->Close());

        winrt::check_hresult(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence_.put())));
    }

    void create_swapchain()
    {
        winrt::com_ptr<IDXGIFactory2> factory;
        winrt::check_hresult(::CreateDXGIFactory2(0, IID_PPV_ARGS(factory.put())));

        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width = width_;
        desc.Height = height_;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.Scaling = DXGI_SCALING_STRETCH;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;

        winrt::com_ptr<IDXGISwapChain1> base;
        winrt::check_hresult(factory->CreateSwapChainForComposition(queue_.get(), &desc, nullptr, base.put()));
        swapchain_ = base.as<IDXGISwapChain3>();

        D3D12_DESCRIPTOR_HEAP_DESC heap_desc{};
        heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        heap_desc.NumDescriptors = 2;
        winrt::check_hresult(device_->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(rtv_heap_.put())));
        rtv_size_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        winrt::check_hresult(
            ::DCompositionCreateDevice3(nullptr, __uuidof(IDCompositionDevice), composition_device_.put_void()));
        winrt::check_hresult(composition_device_->CreateTargetForHwnd(window_, TRUE, composition_target_.put()));
        winrt::check_hresult(composition_device_->CreateVisual(composition_visual_.put()));
        winrt::check_hresult(composition_visual_->SetContent(swapchain_.get()));
        winrt::check_hresult(composition_target_->SetRoot(composition_visual_.get()));
        winrt::check_hresult(composition_device_->Commit());
    }

    void create_rtvs()
    {
        auto handle = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
        for (UINT index = 0; index < 2; ++index)
        {
            backbuffers_[index] = {};
            winrt::check_hresult(swapchain_->GetBuffer(index, IID_PPV_ARGS(backbuffers_[index].put())));
            device_->CreateRenderTargetView(backbuffers_[index].get(), nullptr, handle);
            handle.ptr += rtv_size_;
            buffer_initialized_[index] = false;
        }
    }

    void wait_for_gpu()
    {
        if (!fence_ || !queue_)
        {
            return;
        }

        const auto value = ++fence_value_;
        winrt::check_hresult(queue_->Signal(fence_.get(), value));
        if (fence_->GetCompletedValue() < value)
        {
            winrt::check_hresult(fence_->SetEventOnCompletion(value, fence_event_.get()));
            fence_event_.wait();
        }
    }

    HWND window_{};
    UINT width_{};
    UINT height_{};
    bool initialized_{};
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
    win32::event fence_event_;
    std::uint64_t fence_value_{};
    UINT rtv_size_{};
};

} // namespace corehost::d3d12ui
