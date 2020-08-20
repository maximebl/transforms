#pragma once
#include "common.h"
#include <DirectXCollision.h>
#include "pix3.h"

constexpr int NUM_BACK_BUFFERS = 3;
constexpr int DEFAULT_NODE = 0;

#if defined(_DEBUG) || defined(DBG)
inline void set_name(ID3D12Object *object, LPCWSTR name)
{
    object->SetName(name);
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

#define NAME_D3D12_OBJECT(x) set_name((x), L#x)
#define NAME_D3D12_OBJECT_INDEXED(x, n) set_name_indexed((x), L#x, n)

enum shader_type
{
    vertex = 0,
    domain,
    hull,
    geometry,
    pixel,
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
    std::wstring name;
    std::vector<submesh> submeshes;
    DirectX::BoundingBox bounds;
    UINT cb_index = -1;
    UINT vertex_count = 0;
    UINT index_count = 0;
    mesh_resource *resource;
};

COMMON_API std::vector<mesh_data> import_meshdata(const char *path);
COMMON_API void set_viewport_rects(ID3D12GraphicsCommandList *cmd_list);
COMMON_API D3D12_DEPTH_STENCIL_DESC create_outline_dss();
COMMON_API D3D12_DEPTH_STENCIL_DESC create_stencil_dss();
COMMON_API bool compile_shader(const wchar_t *file, const wchar_t *entry, shader_type type, ID3DBlob **blob);
COMMON_API size_t align_up(size_t value, size_t alignment);
COMMON_API void create_default_buffer(ID3D12Device *device, ID3D12GraphicsCommandList *cmd_list,
                                      const void *data, size_t byte_size,
                                      ID3D12Resource **upload_resource, ID3D12Resource **default_resource, const wchar_t *name);

COMMON_API void create_mesh_data(ID3D12Device *device, ID3D12GraphicsCommandList *cmd_list, const wchar_t *name,
                                 size_t vertex_stride, size_t vertex_count, void *vertex_data,
                                 size_t index_stride, size_t index_count, void *index_data,
                                 mesh *mesh);

class COMMON_API upload_buffer
{
public:
    upload_buffer(ID3D12Device *device, size_t element_count, size_t element_byte_size, const char *name);
    ~upload_buffer();
    void copy_data(int elementIndex, const void *data);
    void clear_data();

    const char *name;
    ID3D12Resource *m_uploadbuffer = nullptr;
    UINT m_buffer_size = 0;
    UINT m_max_element_count = 0;
    BYTE *m_mapped_data = nullptr;
    size_t m_element_byte_size = 0;
};

class COMMON_API device_resources
{
public:
    device_resources();
    ~device_resources();

    void create_rootsig(std::vector<CD3DX12_ROOT_PARAMETER1> *params, const wchar_t *name);
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

    D3D12_CPU_DESCRIPTOR_HANDLE rtv_descriptors[NUM_BACK_BUFFERS];
    ID3D12Resource *main_rt_resources[NUM_BACK_BUFFERS];
    UINT srv_desc_handle_incr_size = 0;
    UINT rtv_handle_incr_size = 0;

    // command objects
    HANDLE cpu_wait_event;
    ID3D12CommandQueue *cmd_queue = nullptr;
    ID3D12Fence *fence = nullptr;
    HANDLE swapchain_event = nullptr; //Signals when the DXGI adapter finished presenting a new frame
    UINT64 last_signaled_fence_value = 0;
    UINT backbuffer_index = 0; //Gets updated after each call to Present()

    // Debug effects
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
};
