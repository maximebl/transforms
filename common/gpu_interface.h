#pragma once
#include "pch.h"
#include "common.h"

#define NUM_BACK_BUFFERS 3
#define DEFAULT_NODE 0

COMMON_API void set_viewport_rects(ID3D12GraphicsCommandList *cmd_list);
COMMON_API bool compile_shader(const wchar_t *file, ID3DBlob **blob);

struct frame_cmd
{
    ID3D12CommandAllocator *cmd_alloc;
    UINT64 fence_value;
};

class COMMON_API device_resources
{
public:
    device_resources();
    ~device_resources();

    void set_viewport_rects(ID3D12GraphicsCommandList *cmd_list);
    void cpu_wait(UINT64 fence_value);
    void flush_cmd_queue();
    void wait_last_present();
    void wait_last_frame();
    void wait_current_frame();
    void signal();
    void present(bool is_vsync);

    // device objects
    ID3D12Device *device;
    IDXGIFactory6 *dxgi_factory;
    ID3D12Debug1 *debug;
    IDXGIAdapter4 *adapter;
    IDXGISwapChain3 *swapchain;

    // core resources
    ID3D12DescriptorHeap *rtv_desc_heap;
    ID3D12DescriptorHeap *imgui_srv_heap;

    ID3D12DescriptorHeap *dsv_heap;
    ID3D12Resource *dsv_resource;
    DXGI_FORMAT dsv_format = DXGI_FORMAT_D24_UNORM_S8_UINT;

    D3D12_CPU_DESCRIPTOR_HANDLE rtv_descriptors[NUM_BACK_BUFFERS];
    ID3D12Resource *main_rt_resources[NUM_BACK_BUFFERS];
    UINT srv_desc_handle_incr_size;
    UINT rtv_handle_incr_size;

    // command objects
    frame_cmd frame_cmds[NUM_BACK_BUFFERS];
    ID3D12CommandQueue *cmd_queue;
    ID3D12Fence *fence;
    HANDLE swapchain_event; //Signals when the DXGI adapter finished presenting a new frame
    UINT64 last_signaled_fence_value;
    UINT backbuffer_index; //Gets updated after each call to Present()
};