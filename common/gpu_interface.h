#pragma once
#include "common.h"
#include <DirectXCollision.h>
#include "pix3.h"
#include <DXProgrammableCapture.h>

constexpr int NUM_BACK_BUFFERS = 3;
constexpr int DEFAULT_NODE = 0;
constexpr int MAX_DEBUG_VERTICES = 1000;
constexpr int MAX_DEBUG_INDICES = 5000;

#if defined(_DEBUG) || defined(DBG)
inline void set_name(ID3D12Object *object, const char *name)
{
    wchar_t obj_name[100] = {};
    mbstowcs(obj_name, name, strlen(name));
    object->SetName(obj_name);
}
inline void set_name_indexed(ID3D12Object *object, LPCWSTR name, UINT index)
{
    WCHAR fullname[50];
    if (swprintf_s(fullname, L"%s[%u]", name, index) > 0)
        object->SetName(fullname);
}
#else
inline void set_name(ID3D12Object *, LPCWSTR)
{
}
inline void set_name_indexed(ID3D12Object *, LPCWSTR, UINT)
{
}
#endif

#define NAME_D3D12_OBJECT(x) set_name((x), #x)
#define NAME_D3D12_OBJECT_INDEXED(x, n) set_name_indexed((x), L#x, n)

enum shader_type
{
    vertex = 0,
    domain,
    hull,
    geometry,
    pixel,
    compute,
};

struct position_color
{
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT4 color;
};

struct mesh_data
{
    std::string name;
    std::vector<position_color> vertices;
    std::vector<WORD> indices;
};

struct submesh
{
    std::string name = "";
    bool is_selected = false;
    UINT vertex_count = 0;
    UINT index_count = 0;
    DirectX::BoundingBox bounds;
};

struct mesh_resource
{
    ID3D12Resource *vertex_default = nullptr;
    ID3D12Resource *vertex_upload = nullptr;
    ID3D12Resource *index_default = nullptr;
    ID3D12Resource *index_upload = nullptr;
    D3D12_VERTEX_BUFFER_VIEW vbv = {};
    D3D12_INDEX_BUFFER_VIEW ibv = {};
};

struct mesh
{
    std::string name;
    std::vector<submesh> submeshes;
    DirectX::BoundingBox bounds;
    INT cb_index = -1;
    UINT vertex_count = 0;
    UINT index_count = 0;
    mesh_resource *resource;
};

COMMON_API std::vector<mesh_data> import_meshdata(const char *path);
COMMON_API void set_viewport_rects(ID3D12GraphicsCommandList *cmd_list);
COMMON_API D3D12_DEPTH_STENCIL_DESC create_outline_dss();
COMMON_API D3D12_DEPTH_STENCIL_DESC create_stencil_dss();
COMMON_API bool compile_shader(const wchar_t *file, const wchar_t *entry, shader_type type, ID3DBlob **blob);
COMMON_API void create_default_buffer(ID3D12Device *device, ID3D12GraphicsCommandList *cmd_list,
                                      const void *data, size_t byte_size,
                                      ID3D12Resource **upload_resource, ID3D12Resource **default_resource, const char *name, D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE);

COMMON_API void create_mesh_data(ID3D12Device *device, ID3D12GraphicsCommandList *cmd_list, const char *name,
                                 size_t vertex_stride, size_t vertex_count, void *vertex_data,
                                 size_t index_stride, size_t index_count, void *index_data,
                                 mesh *mesh);

class COMMON_API upload_buffer
{
public:
    upload_buffer(ID3D12Device *device, size_t element_count, size_t element_byte_size, bool is_constant_buffer, const char *name);
    ~upload_buffer();
    void copy_data(int elementIndex, const void *data);
    void clear_data();

    const char *name;
    ID3D12Resource *m_uploadbuffer = nullptr;
    size_t m_buffer_size = 0;
    UINT m_max_element_count = 0;
    BYTE *m_mapped_data = nullptr;
    size_t m_element_byte_size = 0;
};

//#ifdef DX12_ENABLE_DEBUG_LAYER
//struct COMMON_API debug_utils
//{
//    debug_utils(ID3D12Device *device) : m_last_signaled_fence_value(0), m_device(device)
//    {
//        check_hr(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY, IID_PPV_ARGS(&m_debug_cpy_alloc)));
//        check_hr(device->CreateCommandList(DEFAULT_NODE, D3D12_COMMAND_LIST_TYPE_COPY, m_debug_cpy_alloc, nullptr, IID_PPV_ARGS(&m_debug_cpy_cmdlist)));
//
//        D3D12_COMMAND_QUEUE_DESC queue_desc;
//        queue_desc.NodeMask = DEFAULT_NODE;
//        queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
//        queue_desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
//        queue_desc.Type = D3D12_COMMAND_LIST_TYPE_COPY;
//        check_hr(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&m_debug_cpy_queue)));
//
//        check_hr(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
//
//        m_fence_event = CreateEventEx(nullptr, NULL, NULL, EVENT_ALL_ACCESS);
//    }
//
//    ~debug_utils()
//    {
//    }
//
//    ID3D12Device *m_device;
//    ID3D12CommandAllocator *m_debug_cpy_alloc;
//    ID3D12GraphicsCommandList *m_debug_cpy_cmdlist;
//    ID3D12CommandQueue *m_debug_cpy_queue;
//    ID3D12Fence *m_fence;
//    HANDLE m_fence_event;
//    UINT64 m_last_signaled_fence_value;
//
//    template <typename T>
//    std::vector<T> flush_and_readback(ID3D12Resource *target, D3D12_RESOURCE_STATES target_state, size_t count)
//    {
//        m_debug_cpy_cmdlist->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(target,
//                                                                                      target_state,
//                                                                                      D3D12_RESOURCE_STATE_COPY_SOURCE));
//
//        // Create readback resource
//        ID3D12Resource *rb = nullptr;
//        check_hr(m_device->CreateCommittedResource(
//            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK),
//            D3D12_HEAP_FLAG_NONE,
//            &CD3DX12_RESOURCE_DESC::Buffer(sizeof(T) * count),
//            D3D12_RESOURCE_STATE_COPY_DEST,
//            nullptr,
//            IID_PPV_ARGS(&rb)));
//
//        // Copy target default resource into readback resource
//        m_debug_cpy_cmdlist->CopyResource(rb, target);
//
//        // Map readback resource to CPU pointer
//        T *target_data;
//        rb->Map(0, &CD3DX12_RANGE(0, count * sizeof(T)), (void **)&target_data);
//
//        m_debug_cpy_cmdlist->Close();
//        m_debug_cpy_queue->ExecuteCommandLists(1, (ID3D12CommandList *const *)&m_debug_cpy_cmdlist);
//
//        // Flush command queue
//        m_debug_cpy_queue->Signal(m_fence, ++m_last_signaled_fence_value);
//        m_fence->SetEventOnCompletion(m_last_signaled_fence_value, m_fence_event);
//        UINT64 fence_value = m_fence->GetCompletedValue();
//        if (fence_value <= m_last_signaled_fence_value)
//            WaitForSingleObject(m_fence_event, INFINITE);
//
//        // Delete rb buffer
//        //rb->Unmap(0, nullptr);
//        //m_device->Evict(1, &rb);
//        //safe_release(rb);
//
//        std::vector<T> results(count);
//
//        for (size_t i = 0; i < count; i++)
//        {
//            results[i] = target_data[i];
//        }
//
//        return results;
//    }
//};
//#endif

struct COMMON_API device_resources
{
    device_resources();
    ~device_resources();

    void create_rootsig(std::vector<CD3DX12_ROOT_PARAMETER1> *params, std::vector<CD3DX12_STATIC_SAMPLER_DESC> *samplers);
    void create_rendertargets();
    void create_dsv(UINT64 width, UINT height);
    D3D12_GRAPHICS_PIPELINE_STATE_DESC create_default_pso_desc(std::vector<D3D12_INPUT_ELEMENT_DESC> *input_layouts);
    void resize_swapchain(int width, int height);
    void cleanup_rendertargets();
    void cpu_wait(UINT64 fence_value);
    void cpu_wait_for_present_and_fence(UINT64 fence_value);
    void flush_cmd_queue();
    void wait_present();
    void wait_last_frame();
    UINT64 signal();
    void present(bool is_vsync);
    void resize(int width, int height);
    void begin_capture();
    void end_capture();

    // shader objects
    ID3D12RootSignature *rootsig = nullptr;

    // device objects
    ID3D12Device *device = nullptr;
    IDXGIFactory6 *dxgi_factory = nullptr;
    ID3D12Debug1 *debug = nullptr;
    IDXGIAdapter4 *adapter = nullptr;
    IDXGISwapChain3 *swapchain = nullptr;

    // core resources
    ID3D12DescriptorHeap *rtv_desc_heap = nullptr;
    ID3D12DescriptorHeap *dsv_heap = nullptr;
    ID3D12Resource *dsv_resource = nullptr;
    DXGI_FORMAT dsv_format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    DXGI_FORMAT rtv_format = DXGI_FORMAT_R8G8B8A8_UNORM;
    UINT swapchain_flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT | DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

    D3D12_CPU_DESCRIPTOR_HANDLE rtv_descriptor_handles[NUM_BACK_BUFFERS];
    ID3D12Resource *back_buffers[NUM_BACK_BUFFERS];
    UINT srv_desc_handle_incr_size = 0;
    UINT rtv_handle_incr_size = 0;

    // command objects
    HANDLE cpu_wait_event;
    ID3D12CommandQueue *cmd_queue = nullptr;
    ID3D12Fence *fence = nullptr;
    HANDLE swapchain_event = nullptr; //Signals when the DXGI adapter finished presenting a new frame
    UINT64 last_signaled_fence_value = 0;
    UINT backbuffer_index = 0; //Gets updated after each call to Present()

    // Debugging
    IDXGraphicsAnalysis *graphics_analysis;
    ID3D12GraphicsCommandList *readback_list = nullptr;
    ID3D12CommandAllocator *readback_alloc = nullptr;
    ID3D12Resource *debug_readback_resource = nullptr;
    ID3D12CommandQueue *debug_copy_queue = nullptr;
    UINT64 copy_fence_value = 0;
    ID3D12Fence *copy_fence = nullptr;
    HANDLE copy_fence_event = nullptr;

    //const int max_debug_lines = 1000;
    //upload_buffer *debug_lines_upload = nullptr;
    //D3D12_VERTEX_BUFFER_VIEW debug_lines_vbv = {};
    //struct debug_line
    //{
    //    position_color start;
    //    position_color end;
    //};
    //debug_line orbit_line;
    //std::vector<debug_line *> debug_lines;
    //void draw_debug_lines(ID3D12GraphicsCommandList *cmd_list, std::vector<debug_line *> *debug_lines);
    //bool is_line_buffer_ready = false;
    //ID3D12PipelineState *line_pso = nullptr;
    //ID3DBlob *debugfx_blob_vs = nullptr;
    //ID3DBlob *debugfx_blob_ps = nullptr;

    template <typename T>
    std::vector<T> readback(ID3D12Resource *target, size_t count = 1)
    {
        size_t bytes_to_copy = sizeof(T) * count;
        readback_list->CopyBufferRegion(debug_readback_resource, 0, target, 0, bytes_to_copy);

        T *target_data;
        debug_readback_resource->Map(0, &CD3DX12_RANGE(0, count * sizeof(T)), (void **)&target_data);

        readback_list->Close();
        debug_copy_queue->ExecuteCommandLists(1, (ID3D12CommandList *const *)&readback_list);

        // Flush copy queue
        debug_copy_queue->Signal(copy_fence, ++copy_fence_value);
        copy_fence->SetEventOnCompletion(copy_fence_value, copy_fence_event);

        if (copy_fence_value > copy_fence->GetCompletedValue())
        {
            WaitForSingleObject(copy_fence_event, INFINITE);
        }

        // Read results
        std::vector<T> results(count);

        for (size_t i = 0; i < count; i++)
        {
            results[i] = target_data[i];
        }

        // Prepare for next readback
        readback_alloc->Reset();
        readback_list->Reset(readback_alloc, nullptr);
        debug_readback_resource->Unmap(0, nullptr);

        return results;
    }

    template <typename T>
    std::vector<T> flush_and_readback(ID3D12GraphicsCommandList *cmd_list, ID3D12CommandAllocator *alloc, ID3D12Resource *target, D3D12_RESOURCE_STATES target_state, size_t count = 1)
    {
        cmd_list->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
                                         target,
                                         target_state,
                                         D3D12_RESOURCE_STATE_COPY_SOURCE));
        // Create readback resource
        ID3D12Resource *rb = nullptr;
        check_hr(device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(sizeof(T) * count),
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&rb)));
        NAME_D3D12_OBJECT(rb);

        cmd_list->CopyResource(rb, target);

        T *target_data;
        rb->Map(0, &CD3DX12_RANGE(0, count * sizeof(T)), (void **)&target_data);
        rb->Unmap(0, nullptr);

        cmd_list->Close();
        cmd_queue->ExecuteCommandLists(1, (ID3D12CommandList *const *)&cmd_list);
        flush_cmd_queue();

        alloc->Reset();
        cmd_list->Reset(alloc, nullptr);

        safe_release(rb);

        std::vector<T> results(count);

        for (size_t i = 0; i < count; i++)
        {
            results[i] = target_data[i];
        }

        return results;
    }
};
