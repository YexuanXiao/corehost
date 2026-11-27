#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dcomp.h>
#include <dxgi1_6.h>
#include <winrt/base.h>
#include "utility/log.hpp"
#include "win32/event.hpp"
#include "color_ps.hpp"
#include "text_ps.hpp"
#include "text_vs.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <flat_map>
#include <span>
#include <vector>

namespace corehost::d3d12ui
{

struct text_quad final
{
    float x{};
    float y{};
    float width{};
    float height{};
    float u0{};
    float v0{};
    float u1{};
    float v1{};
    float r{1.0f};
    float g{1.0f};
    float b{1.0f};
    float a{1.0f};
};

struct image_quad final
{
    std::uint32_t texture_id{};
    text_quad quad;
};

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
        create_text_pipeline();
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
        texture_srv_heap_ = {};
        text_pipeline_ = {};
        color_pipeline_ = {};
        root_signature_ = {};
        text_texture_ = {};
        color_text_texture_ = {};
        text_quads_.clear();
        color_text_quads_.clear();
        image_quads_.clear();
        image_textures_.clear();
        free_image_descriptors_.clear();
        image_next_descriptor_ = 0;
        image_srv_heap_ = {};
        image_srv_size_ = 0;
        texture_width_ = 0;
        texture_height_ = 0;
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

    [[nodiscard]] bool device_lost() const noexcept
    {
        return device_ != nullptr && device_->GetDeviceRemovedReason() != S_OK;
    }

    void set_text_texture(std::span<const std::uint8_t> alpha, UINT width, UINT height)
    {
        assert(initialized_);
        assert(width > 0 && height > 0);
        assert(alpha.size() == static_cast<std::size_t>(width) * static_cast<std::size_t>(height));

        wait_for_gpu();
        text_texture_ = {};

        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = width;
        desc.Height = height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        winrt::check_hresult(device_->CreateCommittedResource(
            &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(text_texture_.put())));

        const auto row_pitch = (width + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) &
                               ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);
        const auto upload_size = static_cast<UINT64>(row_pitch) * height;

        D3D12_HEAP_PROPERTIES upload_heap{};
        upload_heap.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC upload_desc{};
        upload_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        upload_desc.Width = upload_size;
        upload_desc.Height = 1;
        upload_desc.DepthOrArraySize = 1;
        upload_desc.MipLevels = 1;
        upload_desc.Format = DXGI_FORMAT_UNKNOWN;
        upload_desc.SampleDesc.Count = 1;
        upload_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        winrt::com_ptr<ID3D12Resource> upload;
        winrt::check_hresult(device_->CreateCommittedResource(
            &upload_heap, D3D12_HEAP_FLAG_NONE, &upload_desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(upload.put())));

        void *mapped = nullptr;
        winrt::check_hresult(upload->Map(0, nullptr, &mapped));
        for (UINT row = 0; row < height; ++row)
        {
            std::memcpy(static_cast<std::uint8_t *>(mapped) + static_cast<std::size_t>(row) * row_pitch,
                        alpha.data() + static_cast<std::size_t>(row) * width, width);
        }
        upload->Unmap(0, nullptr);

        winrt::check_hresult(allocator_->Reset());
        winrt::check_hresult(command_list_->Reset(allocator_.get(), nullptr));

        D3D12_TEXTURE_COPY_LOCATION destination{};
        destination.pResource = text_texture_.get();
        destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        destination.SubresourceIndex = 0;
        D3D12_TEXTURE_COPY_LOCATION source{};
        source.pResource = upload.get();
        source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        source.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8_UNORM;
        source.PlacedFootprint.Footprint.Width = width;
        source.PlacedFootprint.Footprint.Height = height;
        source.PlacedFootprint.Footprint.Depth = 1;
        source.PlacedFootprint.Footprint.RowPitch = row_pitch;
        command_list_->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = text_texture_.get();
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        command_list_->ResourceBarrier(1, &barrier);
        winrt::check_hresult(command_list_->Close());
        ID3D12CommandList *lists[] = {command_list_.get()};
        queue_->ExecuteCommandLists(1, lists);
        wait_for_gpu();

        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = DXGI_FORMAT_R8_UNORM;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MipLevels = 1;
        device_->CreateShaderResourceView(text_texture_.get(), &srv,
                                          texture_srv_heap_->GetCPUDescriptorHandleForHeapStart());
        texture_width_ = width;
        texture_height_ = height;
    }

    void set_color_text_texture(std::span<const std::uint8_t> rgba, UINT width, UINT height)
    {
        assert(initialized_);
        assert(width > 0 && height > 0);
        assert(rgba.size() == static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);

        wait_for_gpu();
        color_text_texture_ = {};

        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = width;
        desc.Height = height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        winrt::check_hresult(device_->CreateCommittedResource(
            &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(color_text_texture_.put())));

        const auto row_bytes = static_cast<UINT>(width) * 4;
        const auto row_pitch = (row_bytes + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) &
                               ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);
        const auto upload_size = static_cast<UINT64>(row_pitch) * height;

        D3D12_HEAP_PROPERTIES upload_heap{};
        upload_heap.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC upload_desc{};
        upload_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        upload_desc.Width = upload_size;
        upload_desc.Height = 1;
        upload_desc.DepthOrArraySize = 1;
        upload_desc.MipLevels = 1;
        upload_desc.Format = DXGI_FORMAT_UNKNOWN;
        upload_desc.SampleDesc.Count = 1;
        upload_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        winrt::com_ptr<ID3D12Resource> upload;
        winrt::check_hresult(device_->CreateCommittedResource(
            &upload_heap, D3D12_HEAP_FLAG_NONE, &upload_desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(upload.put())));

        void *mapped = nullptr;
        winrt::check_hresult(upload->Map(0, nullptr, &mapped));
        for (UINT row = 0; row < height; ++row)
        {
            std::memcpy(static_cast<std::uint8_t *>(mapped) + static_cast<std::size_t>(row) * row_pitch,
                        rgba.data() + static_cast<std::size_t>(row) * row_bytes, row_bytes);
        }
        upload->Unmap(0, nullptr);

        winrt::check_hresult(allocator_->Reset());
        winrt::check_hresult(command_list_->Reset(allocator_.get(), nullptr));

        D3D12_TEXTURE_COPY_LOCATION destination{};
        destination.pResource = color_text_texture_.get();
        destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        destination.SubresourceIndex = 0;
        D3D12_TEXTURE_COPY_LOCATION source{};
        source.pResource = upload.get();
        source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        source.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        source.PlacedFootprint.Footprint.Width = width;
        source.PlacedFootprint.Footprint.Height = height;
        source.PlacedFootprint.Footprint.Depth = 1;
        source.PlacedFootprint.Footprint.RowPitch = row_pitch;
        command_list_->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = color_text_texture_.get();
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        command_list_->ResourceBarrier(1, &barrier);
        winrt::check_hresult(command_list_->Close());
        ID3D12CommandList *lists[] = {command_list_.get()};
        queue_->ExecuteCommandLists(1, lists);
        wait_for_gpu();

        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MipLevels = 1;
        device_->CreateShaderResourceView(color_text_texture_.get(), &srv,
                                          color_srv_heap_->GetCPUDescriptorHandleForHeapStart());
        color_texture_width_ = width;
        color_texture_height_ = height;
    }
    void set_clear_color(float r, float g, float b, float a) noexcept
    {
        clear_color_[0] = r;
        clear_color_[1] = g;
        clear_color_[2] = b;
        clear_color_[3] = a;
    }

    void set_present_dirty_rect(const RECT &rect) noexcept
    {
        dirty_rect_ = rect;
        has_dirty_rect_ = true;
    }

    void set_text_quads(std::span<const text_quad> quads)
    {
        text_quads_.assign(quads.begin(), quads.end());
    }

    void set_color_text_quads(std::span<const text_quad> quads)
    {
        color_text_quads_.assign(quads.begin(), quads.end());
    }

    void set_image_quads(std::span<const image_quad> quads)
    {
        image_quads_.assign(quads.begin(), quads.end());
    }

    bool set_image_texture(std::uint32_t texture_id, std::span<const std::uint8_t> rgba, UINT width, UINT height)
    {
        if (!initialized_ || width == 0 || height == 0 ||
            rgba.size() < static_cast<std::size_t>(width) * height * 4)
        {
            return false;
        }

        wait_for_gpu();
        if (image_srv_heap_ == nullptr)
        {
            D3D12_DESCRIPTOR_HEAP_DESC heap_desc{};
            heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            heap_desc.NumDescriptors = image_srv_capacity;
            heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            winrt::check_hresult(device_->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(image_srv_heap_.put())));
            image_srv_size_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }

        auto found = image_textures_.find(texture_id);
        UINT descriptor_index{};
        if (found != image_textures_.end())
        {
            descriptor_index = found->second.descriptor_index;
            found->second.texture = {};
        }
        else
        {
            if (!free_image_descriptors_.empty())
            {
                descriptor_index = free_image_descriptors_.back();
                free_image_descriptors_.pop_back();
            }
            else if (image_next_descriptor_ < image_srv_capacity)
            {
                descriptor_index = image_next_descriptor_++;
            }
            else
            {
                LOG2("image texture atlas full");
                return false;
            }
        }

        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC resource_desc{};
        resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        resource_desc.Width = width;
        resource_desc.Height = height;
        resource_desc.DepthOrArraySize = 1;
        resource_desc.MipLevels = 1;
        resource_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        resource_desc.SampleDesc.Count = 1;
        winrt::com_ptr<ID3D12Resource> texture;
        winrt::check_hresult(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &resource_desc,
                                                              D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                              IID_PPV_ARGS(texture.put())));

        const auto row_pitch = static_cast<UINT>(width) * 4;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        footprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        footprint.Footprint.Width = width;
        footprint.Footprint.Height = height;
        footprint.Footprint.Depth = 1;
        footprint.Footprint.RowPitch = row_pitch;
        D3D12_RESOURCE_DESC upload_desc{};
        upload_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        upload_desc.Width = static_cast<UINT64>(row_pitch) * height;
        upload_desc.Height = 1;
        upload_desc.DepthOrArraySize = 1;
        upload_desc.MipLevels = 1;
        upload_desc.SampleDesc.Count = 1;
        upload_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        D3D12_HEAP_PROPERTIES upload_heap{};
        upload_heap.Type = D3D12_HEAP_TYPE_UPLOAD;
        winrt::com_ptr<ID3D12Resource> upload;
        winrt::check_hresult(device_->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &upload_desc,
                                                              D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                              IID_PPV_ARGS(upload.put())));

        void *mapped{};
        winrt::check_hresult(upload->Map(0, nullptr, &mapped));
        const auto *source = rgba.data();
        auto *destination = static_cast<std::uint8_t *>(mapped);
        for (UINT row = 0; row < height; ++row)
        {
            std::copy_n(source + static_cast<std::size_t>(row) * width * 4, row_pitch,
                        destination + static_cast<std::size_t>(row) * row_pitch);
        }
        upload->Unmap(0, nullptr);

        winrt::check_hresult(allocator_->Reset());
        winrt::check_hresult(command_list_->Reset(allocator_.get(), nullptr));
        D3D12_TEXTURE_COPY_LOCATION destination_location{};
        destination_location.pResource = texture.get();
        destination_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        destination_location.SubresourceIndex = 0;
        D3D12_TEXTURE_COPY_LOCATION source_location{};
        source_location.pResource = upload.get();
        source_location.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        source_location.PlacedFootprint = footprint;
        command_list_->CopyTextureRegion(&destination_location, 0, 0, 0, &source_location, nullptr);
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = texture.get();
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        command_list_->ResourceBarrier(1, &barrier);
        winrt::check_hresult(command_list_->Close());
        ID3D12CommandList *lists[] = {command_list_.get()};
        queue_->ExecuteCommandLists(1, lists);
        wait_for_gpu();

        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MipLevels = 1;
        auto handle = image_srv_heap_->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>(descriptor_index) * image_srv_size_;
        device_->CreateShaderResourceView(texture.get(), &srv, handle);

        image_textures_.insert_or_assign(texture_id,
                                         image_texture{std::move(texture), width, height, descriptor_index});
        return true;
    }

    void remove_image_texture(std::uint32_t texture_id)
    {
        const auto found = image_textures_.find(texture_id);
        if (found == image_textures_.end())
        {
            return;
        }
        wait_for_gpu();
        free_image_descriptors_.push_back(found->second.descriptor_index);
        image_textures_.erase(found);
    }

    void set_text_correction(float grayscale_enhanced_contrast, const std::array<float, 4> &gamma_ratios) noexcept
    {
        grayscale_enhanced_contrast_ = grayscale_enhanced_contrast;
        gamma_ratios_ = gamma_ratios;
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
        command_list_->ClearRenderTargetView(rtv, clear_color_, 0, nullptr);

        if (text_texture_ && texture_width_ != 0 && texture_height_ != 0)
        {
            D3D12_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_), 0.0f, 1.0f};
            D3D12_RECT scissor{0, 0, static_cast<LONG>(width_), static_cast<LONG>(height_)};
            command_list_->RSSetViewports(1, &viewport);
            command_list_->RSSetScissorRects(1, &scissor);
            command_list_->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
            ID3D12DescriptorHeap *heaps[] = {texture_srv_heap_.get()};
            command_list_->SetDescriptorHeaps(1, heaps);
            command_list_->SetGraphicsRootSignature(root_signature_.get());
            command_list_->SetPipelineState(text_pipeline_.get());
            command_list_->SetGraphicsRootDescriptorTable(1, texture_srv_heap_->GetGPUDescriptorHandleForHeapStart());
            const float correction[8] = {gamma_ratios_[0], gamma_ratios_[1], gamma_ratios_[2], gamma_ratios_[3],
                                          grayscale_enhanced_contrast_, 0.0f, 0.0f, 0.0f};
            command_list_->SetGraphicsRoot32BitConstants(2, 8, correction, 0);

            command_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
            for (const auto &quad : text_quads_)
            {
                const float constants[12] = {
                    (quad.x / static_cast<float>(width_)) * 2.0f - 1.0f,
                    1.0f - (quad.y / static_cast<float>(height_)) * 2.0f,
                    (quad.width / static_cast<float>(width_)) * 2.0f,
                    -(quad.height / static_cast<float>(height_)) * 2.0f,
                    quad.u0,
                    quad.v0,
                    quad.u1,
                    quad.v1,
                    quad.r,
                    quad.g,
                    quad.b,
                    quad.a,
                };
                command_list_->SetGraphicsRoot32BitConstants(0, 12, constants, 0);
                command_list_->DrawInstanced(4, 1, 0, 0);
            }
        }

        if (color_text_texture_ && color_texture_width_ != 0 && color_texture_height_ != 0 &&
            !color_text_quads_.empty())
        {
            D3D12_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_), 0.0f, 1.0f};
            D3D12_RECT scissor{0, 0, static_cast<LONG>(width_), static_cast<LONG>(height_)};
            command_list_->RSSetViewports(1, &viewport);
            command_list_->RSSetScissorRects(1, &scissor);
            command_list_->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
            ID3D12DescriptorHeap *heaps[] = {color_srv_heap_.get()};
            command_list_->SetDescriptorHeaps(1, heaps);
            command_list_->SetGraphicsRootSignature(root_signature_.get());
            command_list_->SetPipelineState(color_pipeline_.get());
            command_list_->SetGraphicsRootDescriptorTable(1, color_srv_heap_->GetGPUDescriptorHandleForHeapStart());
            const float correction[8] = {gamma_ratios_[0], gamma_ratios_[1], gamma_ratios_[2], gamma_ratios_[3],
                                          grayscale_enhanced_contrast_, 0.0f, 0.0f, 0.0f};
            command_list_->SetGraphicsRoot32BitConstants(2, 8, correction, 0);
            command_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

            for (const auto &quad : color_text_quads_)
            {
                const float constants[12] = {
                    (quad.x / static_cast<float>(width_)) * 2.0f - 1.0f,
                    1.0f - (quad.y / static_cast<float>(height_)) * 2.0f,
                    (quad.width / static_cast<float>(width_)) * 2.0f,
                    -(quad.height / static_cast<float>(height_)) * 2.0f,
                    quad.u0,
                    quad.v0,
                    quad.u1,
                    quad.v1,
                    quad.r,
                    quad.g,
                    quad.b,
                    quad.a,
                };
                command_list_->SetGraphicsRoot32BitConstants(0, 12, constants, 0);
                command_list_->DrawInstanced(4, 1, 0, 0);
            }
        }
        if (image_srv_heap_ && !image_quads_.empty())
        {
            D3D12_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_), 0.0f, 1.0f};
            D3D12_RECT scissor{0, 0, static_cast<LONG>(width_), static_cast<LONG>(height_)};
            command_list_->RSSetViewports(1, &viewport);
            command_list_->RSSetScissorRects(1, &scissor);
            command_list_->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
            ID3D12DescriptorHeap *heaps[] = {image_srv_heap_.get()};
            command_list_->SetDescriptorHeaps(1, heaps);
            command_list_->SetGraphicsRootSignature(root_signature_.get());
            command_list_->SetPipelineState(color_pipeline_.get());
            const float correction[8] = {gamma_ratios_[0], gamma_ratios_[1], gamma_ratios_[2], gamma_ratios_[3],
                                          grayscale_enhanced_contrast_, 0.0f, 0.0f, 0.0f};
            command_list_->SetGraphicsRoot32BitConstants(2, 8, correction, 0);
            command_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

            for (const auto &image : image_quads_)
            {
                const auto found = image_textures_.find(image.texture_id);
                if (found == image_textures_.end())
                {
                    continue;
                }
                auto descriptor = image_srv_heap_->GetGPUDescriptorHandleForHeapStart();
                descriptor.ptr += static_cast<SIZE_T>(found->second.descriptor_index) * image_srv_size_;
                command_list_->SetGraphicsRootDescriptorTable(1, descriptor);

                const auto &quad = image.quad;
                const float constants[12] = {
                    (quad.x / static_cast<float>(width_)) * 2.0f - 1.0f,
                    1.0f - (quad.y / static_cast<float>(height_)) * 2.0f,
                    (quad.width / static_cast<float>(width_)) * 2.0f,
                    -(quad.height / static_cast<float>(height_)) * 2.0f,
                    quad.u0,
                    quad.v0,
                    quad.u1,
                    quad.v1,
                    quad.r,
                    quad.g,
                    quad.b,
                    quad.a,
                };
                command_list_->SetGraphicsRoot32BitConstants(0, 12, constants, 0);
                command_list_->DrawInstanced(4, 1, 0, 0);
            }
        }

        D3D12_RESOURCE_BARRIER to_present = to_target;
        to_present.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        to_present.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        command_list_->ResourceBarrier(1, &to_present);

        winrt::check_hresult(command_list_->Close());
        ID3D12CommandList *lists[] = {command_list_.get()};
        queue_->ExecuteCommandLists(1, lists);
        DXGI_PRESENT_PARAMETERS present_parameters{};
        RECT dirty_rect = dirty_rect_;
        const bool use_dirty_rect = has_dirty_rect_ && dirty_rect.left < dirty_rect.right &&
                                    dirty_rect.top < dirty_rect.bottom;
        if (use_dirty_rect)
        {
            present_parameters.DirtyRectsCount = 1;
            present_parameters.pDirtyRects = &dirty_rect;
        }
        auto present_result = swapchain_->Present1(1, 0, &present_parameters);
        if (FAILED(present_result) && use_dirty_rect)
        {
            present_parameters = {};
            present_result = swapchain_->Present1(1, 0, &present_parameters);
        }
        winrt::check_hresult(present_result);
        has_dirty_rect_ = false;
        buffer_initialized_[index] = true;
    }

  private:
    struct image_texture final
    {
        winrt::com_ptr<ID3D12Resource> texture;
        UINT width{};
        UINT height{};
        UINT descriptor_index{};
    };

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

    void create_text_pipeline()
    {
        D3D12_DESCRIPTOR_RANGE srv_range{};
        srv_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srv_range.NumDescriptors = 1;
        srv_range.BaseShaderRegister = 0;
        srv_range.RegisterSpace = 0;
        srv_range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_ROOT_PARAMETER parameters[3]{};
        parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameters[0].Constants.ShaderRegister = 0;
        parameters[0].Constants.RegisterSpace = 0;
        parameters[0].Constants.Num32BitValues = 12;
        parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[1].DescriptorTable.NumDescriptorRanges = 1;
        parameters[1].DescriptorTable.pDescriptorRanges = &srv_range;
        parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameters[2].Constants.ShaderRegister = 1;
        parameters[2].Constants.RegisterSpace = 0;
        parameters[2].Constants.Num32BitValues = 8;
        parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_STATIC_SAMPLER_DESC sampler{};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
        sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
        sampler.MinLOD = 0.0f;
        sampler.MaxLOD = 3.402823466e+38f;
        sampler.ShaderRegister = 0;
        sampler.RegisterSpace = 0;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC signature_desc{};
        signature_desc.NumParameters = 3;
        signature_desc.pParameters = parameters;
        signature_desc.NumStaticSamplers = 1;
        signature_desc.pStaticSamplers = &sampler;
        signature_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        winrt::com_ptr<ID3DBlob> signature;
        winrt::com_ptr<ID3DBlob> error;
        winrt::check_hresult(::D3D12SerializeRootSignature(&signature_desc, D3D_ROOT_SIGNATURE_VERSION_1,
                                                           signature.put(), error.put()));
        winrt::check_hresult(device_->CreateRootSignature(0, signature->GetBufferPointer(),
                                                          signature->GetBufferSize(),
                                                          IID_PPV_ARGS(root_signature_.put())));

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline_desc{};
        pipeline_desc.pRootSignature = root_signature_.get();
        pipeline_desc.VS = {text_vertex_shader, sizeof(text_vertex_shader)};
        pipeline_desc.PS = {text_pixel_shader, sizeof(text_pixel_shader)};
        pipeline_desc.BlendState.RenderTarget[0].BlendEnable = TRUE;
        pipeline_desc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        pipeline_desc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        pipeline_desc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        pipeline_desc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        pipeline_desc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        pipeline_desc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        pipeline_desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        pipeline_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        pipeline_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        pipeline_desc.RasterizerState.DepthClipEnable = TRUE;
        pipeline_desc.DepthStencilState.DepthEnable = FALSE;
        pipeline_desc.DepthStencilState.StencilEnable = FALSE;
        pipeline_desc.SampleMask = UINT_MAX;
        pipeline_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pipeline_desc.NumRenderTargets = 1;
        pipeline_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        pipeline_desc.SampleDesc.Count = 1;
        winrt::check_hresult(device_->CreateGraphicsPipelineState(&pipeline_desc, IID_PPV_ARGS(text_pipeline_.put())));

        D3D12_DESCRIPTOR_HEAP_DESC heap_desc{};
        heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heap_desc.NumDescriptors = 1;
        heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        winrt::check_hresult(device_->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(texture_srv_heap_.put())));

        D3D12_GRAPHICS_PIPELINE_STATE_DESC color_pipeline_desc = pipeline_desc;
        color_pipeline_desc.PS = {color_pixel_shader, sizeof(color_pixel_shader)};
        winrt::check_hresult(
            device_->CreateGraphicsPipelineState(&color_pipeline_desc, IID_PPV_ARGS(color_pipeline_.put())));

        D3D12_DESCRIPTOR_HEAP_DESC color_heap_desc{};
        color_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        color_heap_desc.NumDescriptors = 1;
        color_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        winrt::check_hresult(device_->CreateDescriptorHeap(&color_heap_desc, IID_PPV_ARGS(color_srv_heap_.put())));
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
    RECT dirty_rect_{};
    bool has_dirty_rect_{};
    bool buffer_initialized_[2]{};

    winrt::com_ptr<ID3D12Device> device_;
    winrt::com_ptr<ID3D12CommandQueue> queue_;
    winrt::com_ptr<IDXGISwapChain3> swapchain_;
    static constexpr UINT image_srv_capacity = 1024;
    winrt::com_ptr<ID3D12DescriptorHeap> image_srv_heap_;
    UINT image_srv_size_{};
    UINT image_next_descriptor_{};
    std::vector<UINT> free_image_descriptors_;
    std::flat_map<std::uint32_t, image_texture> image_textures_;
    std::vector<image_quad> image_quads_;
    winrt::com_ptr<IDCompositionDevice> composition_device_;
    winrt::com_ptr<IDCompositionTarget> composition_target_;
    winrt::com_ptr<IDCompositionVisual> composition_visual_;
    winrt::com_ptr<ID3D12DescriptorHeap> rtv_heap_;
    winrt::com_ptr<ID3D12CommandAllocator> allocator_;
    winrt::com_ptr<ID3D12GraphicsCommandList> command_list_;
    winrt::com_ptr<ID3D12Fence> fence_;
    winrt::com_ptr<ID3D12Resource> backbuffers_[2];
    winrt::com_ptr<ID3D12RootSignature> root_signature_;
    winrt::com_ptr<ID3D12PipelineState> text_pipeline_;
    winrt::com_ptr<ID3D12PipelineState> color_pipeline_;
    winrt::com_ptr<ID3D12DescriptorHeap> texture_srv_heap_;
    winrt::com_ptr<ID3D12DescriptorHeap> color_srv_heap_;
    winrt::com_ptr<ID3D12Resource> text_texture_;
    winrt::com_ptr<ID3D12Resource> color_text_texture_;
    UINT texture_width_{};
    UINT texture_height_{};
    UINT color_texture_width_{};
    UINT color_texture_height_{};
    float clear_color_[4]{0.015f, 0.015f, 0.02f, 1.0f};
    float grayscale_enhanced_contrast_{};
    std::array<float, 4> gamma_ratios_{};
    std::vector<text_quad> text_quads_;
    std::vector<text_quad> color_text_quads_;
    win32::event fence_event_;
    std::uint64_t fence_value_{};
    UINT rtv_size_{};
};

} // namespace corehost::d3d12ui
