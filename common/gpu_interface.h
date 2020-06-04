#pragma once
#include "pch.h"
#include "common.h"

#define NUM_BACK_BUFFERS 3
#define DEFAULT_NODE 1

enum shader_type
{
    vertex = 0,
    domain,
    hull,
    geometry,
    pixel,
};

COMMON_API void set_viewport_rects(ID3D12GraphicsCommandList *cmd_list);
COMMON_API bool compile_shader(const wchar_t *file, const wchar_t *entry, shader_type type, ID3DBlob **blob);
COMMON_API size_t align_up(size_t value, size_t alignment);
COMMON_API void create_default_buffer(ID3D12Device *device, ID3D12GraphicsCommandList *cmd_list,
                                      const void *data, size_t byte_size,
                                      ID3D12Resource **upload_resource, ID3D12Resource **default_resource, const char *name);
struct mesh
{
    const char *name;
    UINT cb_index = -1;
    ID3D12Resource *default_resource = nullptr;
    ID3D12Resource *upload_resource = nullptr;
    D3D12_VERTEX_BUFFER_VIEW vbv = {};
};
COMMON_API void create_mesh_data(ID3D12Device *device, ID3D12GraphicsCommandList *cmd_list,
                                 mesh *mesh, size_t stride, size_t count, void *data, const char *name);

class COMMON_API device_resources
{
public:
    device_resources();
    ~device_resources();

    void create_rootsig(std::vector<CD3DX12_ROOT_PARAMETER1> *params, const wchar_t *name);
    void create_rendertargets();
    void create_dsv(UINT64 width, UINT height);
    void resize_swapchain(int width, int height);
    void cleanup_rendertargets();
    void set_viewport_rects(ID3D12GraphicsCommandList *cmd_list);
    void cpu_wait(UINT64 fence_value);
    void flush_cmd_queue();
    void wait_present();
    void wait_last_frame();
    UINT64 signal();
    void present(bool is_vsync);
    void resize(int width, int height);

    // shader objects
    ID3D12RootSignature *rootsig;

    // device objects
    ID3D12Device *device;
    IDXGIFactory6 *dxgi_factory;
    ID3D12Debug1 *debug;
    IDXGIAdapter4 *adapter;
    IDXGISwapChain3 *swapchain;

    // core resources
    ID3D12DescriptorHeap *rtv_desc_heap;
    ID3D12DescriptorHeap *dsv_heap;
    ID3D12Resource *dsv_resource;
    DXGI_FORMAT dsv_format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    DXGI_FORMAT rtv_format = DXGI_FORMAT_R8G8B8A8_UNORM;
    UINT swapchain_flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT | DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

    D3D12_CPU_DESCRIPTOR_HANDLE rtv_descriptors[NUM_BACK_BUFFERS];
    ID3D12Resource *main_rt_resources[NUM_BACK_BUFFERS];
    UINT srv_desc_handle_incr_size;
    UINT rtv_handle_incr_size;

    // command objects
    //frame_cmd frame_cmds[NUM_BACK_BUFFERS];
    ID3D12CommandQueue *cmd_queue;
    ID3D12Fence *fence;
    HANDLE swapchain_event; //Signals when the DXGI adapter finished presenting a new frame
    UINT64 last_signaled_fence_value;
    UINT backbuffer_index; //Gets updated after each call to Present()
};

class COMMON_API upload_buffer
{
public:
    upload_buffer(ID3D12Device *device, UINT element_count, UINT element_byte_size);
    ~upload_buffer();
    void copy_data(int elementIndex, const void *data);

    ID3D12Resource *m_uploadbuffer = nullptr;
    UINT m_buffer_size = 0;
    BYTE *m_mapped_data = nullptr;
    UINT m_element_byte_size = 0;
};