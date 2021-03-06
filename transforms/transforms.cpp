#include "pch.h"
//it's important to include imgui.h before imgui_impl_dx12 and imgui_impl_win32
#include <imgui.h>
#include <imgui_impl_dx12.h>
#include <imgui_impl_win32.h>
#include <common.h>
#include <gpu_query.h>
#include <imgui_helpers.h>

#ifdef DX12_ENABLE_DEBUG_LAYER
#include <dxgidebug.h>
#endif

using namespace DirectX;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

struct model_cb
{
    XMFLOAT4X4 model;
};
UINT model_cb_size = (UINT)align_up(sizeof(model_cb), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);

struct frame_context
{
    ID3D12CommandAllocator *cmd_alloc;
    UINT64 fence_value;
    UINT64 triangle_index;

    ID3D12Resource *modelcb_resource;
    BYTE *cpu_mapped_model_cb;
};

ID3D12Resource *packed_uploader;
BYTE *cpu_mapped_packed;
ID3D12Resource *packed_default_resource;
size_t aligned_packed_uploadheap_size;

#define IDT_TIMER1 1
UINT backbuffer_index = 0; // gets updated after each call to Present()
UINT next_backbuffer_index = 0;
UINT srv_desc_handle_incr_size = 0;
static struct frame_context g_frame_context[NUM_BACK_BUFFERS];
static ID3D12Device *g_device = NULL;
static IDXGIAdapter4 *adapter = NULL;
static ID3D12DescriptorHeap *rtv_desc_heap = NULL;
static ID3D12DescriptorHeap *cbv_srv_uav_heap = NULL;
static ID3D12DescriptorHeap *dsv_heap = NULL;
static ID3D12CommandQueue *g_cmd_queue = NULL;
static std::vector<ID3D12GraphicsCommandList *> cmd_lists;
static ID3D12GraphicsCommandList *g_cmd_list = NULL;
static ID3D12GraphicsCommandList *ui_requests_cmdlist = NULL;
static ID3D12CommandAllocator *ui_requests_cmd_alloc;
static ID3D12Fence *g_fence = NULL;
static ID3D12PipelineState *g_pso = NULL;
static ID3D12PipelineState *quad_pso = NULL;
static ID3D12PipelineState *stencil_pso = NULL;
static ID3D12PipelineState *outline_pso = NULL;
static ID3D12RootSignature *g_rootsig = NULL;
ID3DBlob *tri_vs_blob = NULL;
ID3DBlob *tri_ps_blob = NULL;
ID3DBlob *quad_vs_blob = NULL;
ID3DBlob *quad_ps_blob = NULL;
ID3DBlob *outline_ps_blob = NULL;
ID3DBlob *outline_vs_blob = NULL;

static DXGI_FORMAT rtv_format = DXGI_FORMAT_R8G8B8A8_UNORM;
static DXGI_FORMAT dsv_format = DXGI_FORMAT_D24_UNORM_S8_UINT;
static ID3D12Resource *dsv_resource;
static HANDLE g_fence_event = NULL;
static UINT64 g_fence_last_signaled_value = 0;
static IDXGISwapChain3 *g_swapchain = NULL;
static HANDLE g_hswapchain_waitableobject = NULL; // Signals when the DXGI Adapter finished presenting a new frame
static ID3D12Resource *g_main_rt_resources[NUM_BACK_BUFFERS];
static D3D12_CPU_DESCRIPTOR_HANDLE g_rtv_descriptors[NUM_BACK_BUFFERS];

BYTE *cpu_map_prealloc_modelcb = NULL;
static ID3D12Resource *preallocated_modelcb_resource = NULL;
bool is_resource_created = false;

static gpu_query *queries;
static UINT64 *timestamp_buffer = NULL;
static ID3D12Resource *query_rb_buffer;
static ID3D12QueryHeap *query_heap;
static double frame_gpu_time = 0.0;
static double imgui_gpu_time = 0.0;
static double triangle_gpu_time = 0.0;
static double quads_and_tri_gpu_time = 0.0;
static double quads_gpu_time = 0.0;
static UINT total_timer_count = 6;
static UINT ui_timer_count = 6;
UINT stats_counter = 0;
void frame_time_statistics();

static bool is_vsync = false;
bool show_demo_window = true;

// benchmarking
#define microsecond 1000000
#define millisecond 1000
extern double g_cpu_frequency;
extern double g_gpu_frequency;
struct measurement
{
    double start_time;
    double end_time;
    double elapsed_ms;
};

double g_cpu_frequency = 0;
double g_gpu_frequency = 0;
measurement delta_time;
#define BUFFERED_FRAME_STATS 200
double delta_times[BUFFERED_FRAME_STATS];
double delta_time_avg = 0;

extern "C" __declspec(dllexport) bool update_and_render();
extern "C" __declspec(dllexport) void resize(int width, int height);
extern "C" __declspec(dllexport) void cleanup();
extern "C" __declspec(dllexport) void wndproc(UINT msg, WPARAM wParam, LPARAM lParam);
extern "C" __declspec(dllexport) bool initialize();

// triangle creation
#define triangle_vertices_count 3
struct triangle_data
{
    float position[4];
    float color[4];
};

struct quad_vert2d
{
    float position[2];
    float color[3];
};

struct triangle_mesh
{
    ID3D12Resource *vertex_default_resource = NULL;
    ID3D12Resource *vertex_upload_resource = NULL;
    ID3D12Resource *model_cb_resource = NULL;
    model_cb *cpu_mapped_model_cb = NULL;
    D3D12_VERTEX_BUFFER_VIEW vbv = {};
    size_t cb_index = 0;
};

struct quad
{
    D3D12_VERTEX_BUFFER_VIEW vbv = {};
    D3D12_INDEX_BUFFER_VIEW ibv = {};
    size_t cb_index = 0;
    ImVec2 position;
    BoundingBox bounds;
};
std::vector<quad> quads;

#define max_tris 5000
#define max_quads 5000
#define indices_per_quad 6
#define cb_per_quad 1
#define vertices_per_quad 4
std::vector<triangle_mesh> triangles;
enum cbv_creation_options
{
    committed_resource_per_cbv = 0,
    committed_resource_per_frame = 1,
    committed_resource_multiple_cbv = 2,
};
cbv_creation_options cbv_creation_option;

int total_tris_torender = 0;
int total_tris_torender_input = 0;
int total_quads_torender = 0;
int tri_instance_count = 1;
int num_tris_rendered = 0;
int num_missing_tris = 0;
int num_descr_handles = 0;

void init_pipeline();
bool create_device();
void cleanup_device();
void resize_swapchain(HWND hWnd, int width, int height);
void cleanup_rendertarget();
void create_rendertarget();
void create_dsv(UINT64 width, UINT height);
void create_query_objects();
void compile_shader(const wchar_t *file, ID3DBlob **vs_blob, ID3DBlob **ps_blob);
void create_cb_resource_per_cbv(int triangle_index, size_t heap_index);
void append_cb_toheap(int triangle_index, size_t heap_index);
void create_cb_resource_per_frame(int triangle_index);
void fill_packed_resource(
    size_t vertex_size, size_t vertex_count, quad_vert2d *vertices,
    size_t index_size, size_t index_count, WORD *indices,
    size_t heap_index);
void create_quads(int count);

// synchronization
void WaitForLastSubmittedFrame();
frame_context *WaitForNextFrameResources();
void cpu_wait(UINT64 fence_value);

extern "C" __declspec(dllexport) bool initialize()
{
    if (!XMVerifyCPUSupport())
        return false;

    if (!create_device())
    {
        cleanup_device();
        return false;
    }

    imgui_init(g_device);

    UINT64 tmp_gpu_frequency;
    g_cmd_queue->GetTimestampFrequency(&tmp_gpu_frequency);
    g_gpu_frequency = (double)tmp_gpu_frequency;

    LARGE_INTEGER tmp_cpu_frequency;
    QueryPerformanceFrequency(&tmp_cpu_frequency);
    g_cpu_frequency = (double)tmp_cpu_frequency.QuadPart;

    SetTimer(g_hwnd, IDT_TIMER1, 5000, NULL);
    queries = new gpu_query(g_device, g_cmd_list, g_cmd_queue, &backbuffer_index, 5);

    compile_shader(L"..\\..\\transforms\\outline.hlsl", &outline_vs_blob, &outline_ps_blob);
    compile_shader(L"..\\..\\transforms\\default_shader.hlsl", &tri_vs_blob, &tri_ps_blob);
    compile_shader(L"..\\..\\transforms\\quad.hlsl", &quad_vs_blob, &quad_ps_blob);
    init_pipeline();

    return true;
}

extern "C" __declspec(dllexport) void cleanup(void)
{
    KillTimer(g_hwnd, IDT_TIMER1);
    cpu_wait(g_fence_last_signaled_value);
    imgui_shutdown();
    cleanup_device();
}

void frame_time_statistics()
{
    double sum = 0;
    for (UINT i = 0; i < stats_counter; ++i)
    {
        sum += delta_times[i];
    }
    delta_time_avg = sum / stats_counter;
    memset(&delta_times, 0, stats_counter);
}

extern "C" __declspec(dllexport) void wndproc(UINT msg, WPARAM wParam, LPARAM lParam)
{
    ImGui_ImplWin32_WndProcHandler(g_hwnd, msg, wParam, lParam);

    switch (msg)
    {
    case WM_TIMER:
        switch (wParam)
        {
        case IDT_TIMER1:
            frame_time_statistics();
            return;
        }
        break;
    }
}

bool create_device()
{
    DXGI_SWAP_CHAIN_DESC1 sd;
    {
        ZeroMemory(&sd, sizeof(sd));
        sd.BufferCount = NUM_BACK_BUFFERS;
        sd.Width = 0;
        sd.Height = 0;
        sd.Format = DXGI_FORMAT::DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.Flags = DXGI_SWAP_CHAIN_FLAG::DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT | DXGI_SWAP_CHAIN_FLAG::DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.SampleDesc.Count = 1;
        sd.SampleDesc.Quality = 0;
        sd.SwapEffect = DXGI_SWAP_EFFECT::DXGI_SWAP_EFFECT_FLIP_DISCARD;
        sd.AlphaMode = DXGI_ALPHA_MODE::DXGI_ALPHA_MODE_UNSPECIFIED;
        sd.Scaling = DXGI_SCALING::DXGI_SCALING_STRETCH;
        sd.Stereo = FALSE;
    }

#ifdef DX12_ENABLE_DEBUG_LAYER
    ID3D12Debug1 *dx12debug = NULL;
    if (SUCCEEDED(D3D12GetDebugInterface(__uuidof(ID3D12Debug1), (void **)&dx12debug)))
    {
        dx12debug->EnableDebugLayer();
        dx12debug->SetEnableGPUBasedValidation(true);
        dx12debug->Release();
    }
#endif

    IDXGIFactory6 *dxgi_factory = NULL;
    hr = CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, __uuidof(IDXGIFactory6), (void **)&dxgi_factory);
    ASSERT(SUCCEEDED(hr));

    hr = dxgi_factory->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE::DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, __uuidof(IDXGIAdapter4), (void **)&adapter);
    ASSERT(SUCCEEDED(hr));

    hr = D3D12CreateDevice((IUnknown *)adapter, D3D_FEATURE_LEVEL::D3D_FEATURE_LEVEL_12_1, __uuidof(ID3D12Device), (void **)&g_device);
    ASSERT(SUCCEEDED(hr));

    g_device->SetName(L"main_device");

    {
        D3D12_DESCRIPTOR_HEAP_DESC desc;
        desc.NodeMask = 0;
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE::D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        desc.NumDescriptors = NUM_BACK_BUFFERS;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAGS::D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

        hr = g_device->CreateDescriptorHeap(&desc, __uuidof(ID3D12DescriptorHeap), (void **)&rtv_desc_heap);
        ASSERT(SUCCEEDED(hr));

        rtv_desc_heap->SetName(L"main_rtv_desc_heap");
        size_t rtv_descriptor_size = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE::D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle = rtv_desc_heap->GetCPUDescriptorHandleForHeapStart();

        for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
        {
            g_rtv_descriptors[i] = rtv_handle;
            rtv_handle.ptr += rtv_descriptor_size;
        }
    }

    {
        D3D12_DESCRIPTOR_HEAP_DESC main_heap_desc;
        main_heap_desc.NodeMask = 0;
        main_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE::D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        main_heap_desc.NumDescriptors = max_tris;
        main_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAGS::D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        hr = g_device->CreateDescriptorHeap(&main_heap_desc, __uuidof(ID3D12DescriptorHeap), (void **)&cbv_srv_uav_heap);

        ASSERT(SUCCEEDED(hr));
        cbv_srv_uav_heap->SetName(L"tri_cbv_srv_uav_heap");
    }

    {
        D3D12_DESCRIPTOR_HEAP_DESC desc;
        desc.NodeMask = 0;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAGS::D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        desc.NumDescriptors = 1;
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE::D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        hr = g_device->CreateDescriptorHeap(&desc, __uuidof(ID3D12DescriptorHeap), (void **)&dsv_heap);

        ASSERT(SUCCEEDED(hr));
        dsv_heap->SetName(L"main_dsv_heap");
    }

    {
        D3D12_COMMAND_QUEUE_DESC desc;
        desc.NodeMask = 0;
        desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY::D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
        desc.Type = D3D12_COMMAND_LIST_TYPE::D3D12_COMMAND_LIST_TYPE_DIRECT;
        desc.Flags = D3D12_COMMAND_QUEUE_FLAGS::D3D12_COMMAND_QUEUE_FLAG_NONE;
        hr = g_device->CreateCommandQueue(&desc, __uuidof(ID3D12CommandQueue), (void **)&g_cmd_queue);

        ASSERT(SUCCEEDED(hr));
        g_cmd_queue->SetName(L"main_cmd_queue");
    }

    for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
    {
        hr = g_device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE::D3D12_COMMAND_LIST_TYPE_DIRECT,
            __uuidof(ID3D12CommandAllocator),
            (void **)&g_frame_context[i].cmd_alloc);
        ASSERT(SUCCEEDED(hr));

        wchar_t buf[50];
        swprintf_s(buf, 50, L"%s%d", L"main_cmd_alloc_", i);
        g_frame_context[i].cmd_alloc->SetName(buf);

        // upload_heap for triangles
        hr = g_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE::D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAGS::D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(model_cb_size * max_tris),
            D3D12_RESOURCE_STATES::D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr, __uuidof(ID3D12Resource), (void **)&g_frame_context[i].modelcb_resource);
        ASSERT(SUCCEEDED(hr));

        D3D12_RANGE range = {};
        hr = g_frame_context[i].modelcb_resource->Map(0, &range, (void **)&g_frame_context[i].cpu_mapped_model_cb);
        ASSERT(SUCCEEDED(hr));
    }

    // ui command objects
    hr = g_device->CreateCommandList(0,
                                     D3D12_COMMAND_LIST_TYPE::D3D12_COMMAND_LIST_TYPE_DIRECT,
                                     g_frame_context[0].cmd_alloc, NULL,
                                     __uuidof(ID3D12GraphicsCommandList), (void **)&g_cmd_list);
    ASSERT(SUCCEEDED(hr));
    g_cmd_list->SetName(L"main_cmd_list");

    hr = g_device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE::D3D12_COMMAND_LIST_TYPE_DIRECT,
        __uuidof(ID3D12CommandAllocator),
        (void **)&ui_requests_cmd_alloc);
    ASSERT(SUCCEEDED(hr));
    ui_requests_cmd_alloc->SetName(L"ui_cmd_alloc");

    hr = g_device->CreateCommandList(0,
                                     D3D12_COMMAND_LIST_TYPE::D3D12_COMMAND_LIST_TYPE_DIRECT,
                                     ui_requests_cmd_alloc, NULL,
                                     __uuidof(ID3D12GraphicsCommandList), (void **)&ui_requests_cmdlist);
    ASSERT(SUCCEEDED(hr));
    ui_requests_cmdlist->SetName(L"ui_cmd_list");
    ui_requests_cmdlist->Close();

    hr = g_device->CreateFence(0, D3D12_FENCE_FLAGS::D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void **)&g_fence);
    ASSERT(SUCCEEDED(hr));
    g_fence->SetName(L"main_fence");

    g_fence_event = CreateEvent(NULL, FALSE, FALSE, NULL);

    IDXGISwapChain1 *swap_chain = NULL;
    hr = dxgi_factory->CreateSwapChainForHwnd(
        (IUnknown *)g_cmd_queue,
        g_hwnd,
        &sd,
        NULL,
        NULL,
        &swap_chain);
    ASSERT(SUCCEEDED(hr));

    hr = swap_chain->QueryInterface(__uuidof(IDXGISwapChain1), (void **)&(g_swapchain));
    ASSERT(SUCCEEDED(hr));
    swap_chain->Release();

    dxgi_factory->Release();
    hr = g_swapchain->SetMaximumFrameLatency(NUM_BACK_BUFFERS);
    ASSERT(SUCCEEDED(hr));

    g_hswapchain_waitableobject = g_swapchain->GetFrameLatencyWaitableObject();

    // packed quads
    size_t packed_uploadheap_size = (cb_per_quad * sizeof(model_cb) +
                                     (indices_per_quad * sizeof(WORD)) +
                                     (vertices_per_quad * sizeof(quad_vert2d))) *
                                    max_quads;
    aligned_packed_uploadheap_size = align_up(packed_uploadheap_size, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);

    hr = g_device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE::D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAGS::D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(aligned_packed_uploadheap_size),
        D3D12_RESOURCE_STATES::D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr, __uuidof(ID3D12Resource), (void **)&packed_uploader);
    packed_uploader->SetName(L"packed_uploader");
    ASSERT(SUCCEEDED(hr));

    D3D12_RANGE range = {};
    hr = packed_uploader->Map(0, &range, (void **)&cpu_mapped_packed);
    ASSERT(SUCCEEDED(hr));

    hr = g_device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE::D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAGS::D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(aligned_packed_uploadheap_size),
        D3D12_RESOURCE_STATES::D3D12_RESOURCE_STATE_COPY_DEST,
        NULL, __uuidof(ID3D12Resource), (void **)&packed_default_resource);
    ASSERT(SUCCEEDED(hr));
    packed_default_resource->SetName(L"packed_default_resource");

    create_rendertarget();
    create_dsv(g_hwnd_width, g_hwnd_height);
    create_query_objects();

    srv_desc_handle_incr_size = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE::D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // flush command queue
    g_cmd_list->Close();
    g_cmd_queue->ExecuteCommandLists(1, (ID3D12CommandList *const *)&g_cmd_list);
    g_cmd_queue->Signal(g_fence, 1);
    cpu_wait(1);

    return true;
}

void create_dsv(UINT64 width, UINT height)
{
    D3D12_CLEAR_VALUE clear_value;
    clear_value.DepthStencil.Depth = 1.0f;
    clear_value.DepthStencil.Stencil = 0;
    clear_value.Format = dsv_format;

    hr = g_device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE::D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAGS::D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Tex2D(
            dsv_format,
            width,
            height,
            1, 0, 1, 0,
            D3D12_RESOURCE_FLAGS::D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL),
        D3D12_RESOURCE_STATES::D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear_value,
        __uuidof(ID3D12Resource), (void **)&dsv_resource);
    dsv_resource->SetName(L"dsv_resource");
    ASSERT(SUCCEEDED(hr));

    D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc;
    dsv_desc.Flags = D3D12_DSV_FLAGS::D3D12_DSV_FLAG_NONE;
    dsv_desc.Format = dsv_format;
    dsv_desc.Texture2D.MipSlice = 0;
    dsv_desc.ViewDimension = D3D12_DSV_DIMENSION::D3D12_DSV_DIMENSION_TEXTURE2D;

    g_device->CreateDepthStencilView(
        dsv_resource,
        &dsv_desc,
        dsv_heap->GetCPUDescriptorHandleForHeapStart());
}

void create_rendertarget()
{
    for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
    {
        ID3D12Resource *back_buffer = NULL;
        g_swapchain->GetBuffer(i, __uuidof(ID3D12Resource), (void **)&back_buffer);
        g_device->CreateRenderTargetView(back_buffer, NULL, g_rtv_descriptors[i]);

        g_main_rt_resources[i] = back_buffer;

        wchar_t buffer[20];
        swprintf_s(buffer, 20, L"%s%d", L"rtv_resource_", i);
        g_main_rt_resources[i]->SetName(buffer);
    }
}

void cleanup_rendertarget()
{
    WaitForLastSubmittedFrame();

    for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
        if (g_main_rt_resources[i])
        {
            g_main_rt_resources[i]->Release();
            g_main_rt_resources[i] = NULL;
        }
}

s_internal void init_pipeline()
{
    ID3DBlob *rs_blob = NULL;

    D3D12_FEATURE_DATA_ROOT_SIGNATURE feature_data = {};
    feature_data.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
    hr = g_device->CheckFeatureSupport(D3D12_FEATURE::D3D12_FEATURE_ROOT_SIGNATURE, (void *)&feature_data, sizeof(feature_data));
    ASSERT(SUCCEEDED(hr));

    // (root) ConstantBuffer<model_to_proj> cb_modelproj : register(b0);
    D3D12_ROOT_PARAMETER1 parameters[2];
    D3D12_ROOT_PARAMETER1 param_modelproj;
    param_modelproj.ParameterType = D3D12_ROOT_PARAMETER_TYPE::D3D12_ROOT_PARAMETER_TYPE_CBV;
    param_modelproj.ShaderVisibility = D3D12_SHADER_VISIBILITY::D3D12_SHADER_VISIBILITY_VERTEX;
    param_modelproj.Descriptor = {0, 0, D3D12_ROOT_DESCRIPTOR_FLAGS::D3D12_ROOT_DESCRIPTOR_FLAG_NONE};
    parameters[0] = param_modelproj;

    // ConstantBuffer<model> cb_model : register(b1);
    D3D12_DESCRIPTOR_RANGE1 model_cbv_range;
    model_cbv_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE::D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    model_cbv_range.BaseShaderRegister = 1;
    model_cbv_range.RegisterSpace = 0;
    model_cbv_range.NumDescriptors = 1;
    model_cbv_range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    model_cbv_range.Flags = D3D12_DESCRIPTOR_RANGE_FLAGS::D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;

    D3D12_ROOT_DESCRIPTOR_TABLE1 model_cbv_table;
    model_cbv_table.NumDescriptorRanges = 1;
    model_cbv_table.pDescriptorRanges = &model_cbv_range;

    D3D12_ROOT_PARAMETER1 param_model;
    param_model.ParameterType = D3D12_ROOT_PARAMETER_TYPE::D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    param_model.ShaderVisibility = D3D12_SHADER_VISIBILITY::D3D12_SHADER_VISIBILITY_VERTEX;
    param_model.DescriptorTable = model_cbv_table;
    parameters[1] = param_model;

    // Root Signature
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC versioned_rootsig_desc;
    versioned_rootsig_desc.Version = D3D_ROOT_SIGNATURE_VERSION::D3D_ROOT_SIGNATURE_VERSION_1_1;
    versioned_rootsig_desc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAGS::D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    versioned_rootsig_desc.Desc_1_1.NumParameters = _countof(parameters);
    versioned_rootsig_desc.Desc_1_1.pParameters = parameters;
    versioned_rootsig_desc.Desc_1_1.NumStaticSamplers = 0;
    versioned_rootsig_desc.Desc_1_1.pStaticSamplers = NULL;

    ID3DBlob *error_blob = NULL;
    hr = D3D12SerializeVersionedRootSignature(&versioned_rootsig_desc, &rs_blob, &error_blob);
    ASSERT(SUCCEEDED(hr));

    if (error_blob)
    {
        wchar_t *error_msg = (wchar_t *)error_blob->GetBufferPointer();
        OutputDebugStringW(error_msg);
    }

    hr = g_device->CreateRootSignature(
        1,
        rs_blob->GetBufferPointer(),
        rs_blob->GetBufferSize(),
        __uuidof(ID3D12RootSignature),
        (void **)&g_rootsig);
    ASSERT(SUCCEEDED(hr));
    g_rootsig->SetName(L"main_rootsig");

    // triangle pso
    D3D12_INPUT_ELEMENT_DESC input_elem_descs[2] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};

    D3D12_INPUT_LAYOUT_DESC input_layout;
    input_layout.NumElements = _countof(input_elem_descs);
    input_layout.pInputElementDescs = input_elem_descs;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {};
    pso_desc.InputLayout = input_layout;
    pso_desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    pso_desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    pso_desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    pso_desc.pRootSignature = g_rootsig;
    pso_desc.NodeMask = 0;
    pso_desc.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE::D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
    pso_desc.VS = {tri_vs_blob->GetBufferPointer(), tri_vs_blob->GetBufferSize()};
    pso_desc.PS = {tri_ps_blob->GetBufferPointer(), tri_ps_blob->GetBufferSize()};
    pso_desc.RTVFormats[0] = rtv_format;
    pso_desc.DSVFormat = dsv_format;
    pso_desc.NumRenderTargets = 1;
    pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE::D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso_desc.SampleDesc.Count = 1;
    pso_desc.SampleDesc.Quality = 0;
    pso_desc.SampleMask = UINT_MAX;
    pso_desc.Flags = D3D12_PIPELINE_STATE_FLAGS::D3D12_PIPELINE_STATE_FLAG_NONE;
    hr = g_device->CreateGraphicsPipelineState(&pso_desc, __uuidof(ID3D12PipelineState), (void **)&g_pso);
    g_pso->SetName(L"main_pso");
    ASSERT(SUCCEEDED(hr));

    // quad pso
    D3D12_GRAPHICS_PIPELINE_STATE_DESC quad_pso_desc = pso_desc;

    D3D12_INPUT_ELEMENT_DESC quad_input_elem_desc[2] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};

    input_layout.NumElements = _countof(input_elem_descs);
    input_layout.pInputElementDescs = quad_input_elem_desc;

    quad_pso_desc.InputLayout = input_layout;
    quad_pso_desc.VS = {quad_vs_blob->GetBufferPointer(), quad_vs_blob->GetBufferSize()};
    quad_pso_desc.PS = {quad_ps_blob->GetBufferPointer(), quad_ps_blob->GetBufferSize()};
    hr = g_device->CreateGraphicsPipelineState(&quad_pso_desc, __uuidof(ID3D12PipelineState), (void **)&quad_pso);
    quad_pso->SetName(L"quad_pso");
    ASSERT(SUCCEEDED(hr));

    // Stencil PSO
    D3D12_BLEND_DESC selection_stencil_bs = {};
    selection_stencil_bs.RenderTarget[0].RenderTargetWriteMask = 0; // dont write to render targets

    D3D12_DEPTH_STENCIL_DESC stencil_dss = {};
    stencil_dss.DepthWriteMask = D3D12_DEPTH_WRITE_MASK::D3D12_DEPTH_WRITE_MASK_ZERO; // dont write to the depth buffer
    stencil_dss.StencilEnable = true;
    stencil_dss.DepthEnable = true;
    stencil_dss.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
    stencil_dss.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
    stencil_dss.DepthFunc = D3D12_COMPARISON_FUNC::D3D12_COMPARISON_FUNC_LESS;

    stencil_dss.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC::D3D12_COMPARISON_FUNC_ALWAYS; // always write to the stencil buffer
    stencil_dss.FrontFace.StencilPassOp = D3D12_STENCIL_OP::D3D12_STENCIL_OP_REPLACE;
    stencil_dss.FrontFace.StencilFailOp = D3D12_STENCIL_OP::D3D12_STENCIL_OP_KEEP;
    stencil_dss.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP::D3D12_STENCIL_OP_KEEP;
    stencil_dss.BackFace = stencil_dss.FrontFace;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC selection_stencil_pso = quad_pso_desc;
    selection_stencil_pso.BlendState = selection_stencil_bs;
    selection_stencil_pso.DepthStencilState = stencil_dss;

    hr = g_device->CreateGraphicsPipelineState(&selection_stencil_pso, __uuidof(ID3D12PipelineState), (void **)&stencil_pso);
    ASSERT(SUCCEEDED(hr));

    // Outline PSO.
    D3D12_DEPTH_STENCIL_DESC outline_dss = {};
    outline_dss.DepthWriteMask = D3D12_DEPTH_WRITE_MASK::D3D12_DEPTH_WRITE_MASK_ALL;
    outline_dss.DepthEnable = true;
    outline_dss.StencilEnable = true;
    outline_dss.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
    outline_dss.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
    outline_dss.DepthFunc = D3D12_COMPARISON_FUNC::D3D12_COMPARISON_FUNC_LESS;

    outline_dss.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC::D3D12_COMPARISON_FUNC_NOT_EQUAL;
    outline_dss.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP::D3D12_STENCIL_OP_KEEP;
    outline_dss.FrontFace.StencilPassOp = D3D12_STENCIL_OP::D3D12_STENCIL_OP_KEEP;
    outline_dss.FrontFace.StencilFailOp = D3D12_STENCIL_OP::D3D12_STENCIL_OP_KEEP;
    outline_dss.BackFace = outline_dss.FrontFace;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC outline_pso_desc = quad_pso_desc;
    outline_pso_desc.DepthStencilState = outline_dss;
    outline_pso_desc.PS.BytecodeLength = outline_ps_blob->GetBufferSize();
    outline_pso_desc.PS.pShaderBytecode = outline_ps_blob->GetBufferPointer();
    outline_pso_desc.VS.BytecodeLength = outline_vs_blob->GetBufferSize();
    outline_pso_desc.VS.pShaderBytecode = outline_vs_blob->GetBufferPointer();

    hr = g_device->CreateGraphicsPipelineState(&outline_pso_desc, __uuidof(ID3D12PipelineState), (void **)&outline_pso);
    ASSERT(SUCCEEDED(hr));
}

void cpu_wait(UINT64 fence_value)
{
    if (fence_value == 0)
        return; // No fence was signaled
    if (g_fence->GetCompletedValue() >= fence_value)
        return; // We're already exactly at that fence value, or past that fence value

    g_fence->SetEventOnCompletion(fence_value, g_fence_event);

    WaitForSingleObject(g_fence_event, INFINITE);
}

void WaitForLastSubmittedFrame()
{
    frame_context *frame_ctx = &g_frame_context[next_backbuffer_index];

    UINT64 fence_value = frame_ctx->fence_value;
    if (fence_value == 0)
        return; // No fence was signaled

    frame_ctx->fence_value = 0;
    if (g_fence->GetCompletedValue() >= fence_value)
        return;

    // The commented PIX code here shows how you would use WinPIXEventRuntime to display sync events

    /*PIXBeginEvent_gpu(g_cmd_queue, PIX_COLOR(0, 0, 0), "WaitForLastSubmittedFrame");*/
    DWORD wait_result = WaitForSingleObject(g_fence_event, INFINITE);
    /*PIXEndEvent_gpu(g_cmd_queue);*/

    switch (wait_result)
    {
    case WAIT_OBJECT_0:
        PIXNotifyWakeFromFenceSignal(g_fence_event); // The event was successfully signaled, so notify PIX*/
        break;
    }
    g_fence->SetEventOnCompletion(fence_value, g_fence_event);
}

//wait for both the swapchain to finish and the current frame fence value to be reached.
frame_context *WaitForNextFrameResources()
{
    HANDLE waitable_objects[] = {g_hswapchain_waitableobject, NULL};
    DWORD num_waitable_obj = 1;

    frame_context *frame_ctx = &g_frame_context[next_backbuffer_index];
    UINT64 fence_value = frame_ctx->fence_value;
    if (fence_value != 0)
    {
        frame_ctx->fence_value = 0;
        g_fence->SetEventOnCompletion(fence_value, g_fence_event);
        waitable_objects[1] = g_fence_event;
        num_waitable_obj = 2;
    }
    DWORD wait_result = WaitForMultipleObjects(num_waitable_obj, waitable_objects, TRUE, INFINITE);

    switch (wait_result)
    {
    case WAIT_OBJECT_0:
        PIXNotifyWakeFromFenceSignal(g_fence_event); // The event was successfully signaled, so notify PIX*/
        break;
    }

    return frame_ctx;
}

void create_query_objects()
{
    D3D12_QUERY_HEAP_DESC query_heap_desc;
    query_heap_desc.Count = total_timer_count;
    query_heap_desc.NodeMask = 0;
    query_heap_desc.Type = D3D12_QUERY_HEAP_TYPE::D3D12_QUERY_HEAP_TYPE_TIMESTAMP;

    hr = g_device->CreateQueryHeap(
        &query_heap_desc,
        __uuidof(ID3D12QueryHeap),
        (void **)&query_heap);
    ASSERT(SUCCEEDED(hr));
    query_heap->SetName(L"timestamp_query_heap");

    hr = g_device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE::D3D12_HEAP_TYPE_READBACK),
        D3D12_HEAP_FLAGS::D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(sizeof(UINT64) * total_timer_count),
        D3D12_RESOURCE_STATES::D3D12_RESOURCE_STATE_COPY_DEST, NULL,
        __uuidof(ID3D12Resource),
        (void **)&query_rb_buffer);

    ASSERT(SUCCEEDED(hr));
    query_rb_buffer->SetName(L"queries_rb_resource");
}

void compile_shader(const wchar_t *file, ID3DBlob **vs_blob, ID3DBlob **ps_blob)
{
    WIN32_FIND_DATAW found_file;
    if (FindFirstFileW(file, &found_file) == INVALID_HANDLE_VALUE)
    {
        if (MessageBoxW(NULL, L"Required shader file not found.\n\nMake sure default_shaders.hlsl is in the transforms folder.",
                        L"Could not find required shader.",
                        MB_OK | MB_ICONERROR | MB_DEFBUTTON2) != IDYES)
        {
        }
    }

    ID3DBlob *error_blob = NULL;

    hr = D3DCompileFromFile(file,
                            NULL,
                            D3D_COMPILE_STANDARD_FILE_INCLUDE,
                            "VS",
                            "vs_5_1",
                            D3DCOMPILE_SKIP_OPTIMIZATION | D3DCOMPILE_DEBUG,
                            0,
                            vs_blob,
                            &error_blob);
    if (error_blob)
    {
        char *error_msg = (char *)error_blob->GetBufferPointer();
        OutputDebugStringA(error_msg);
    }
    ASSERT(SUCCEEDED(hr));

    hr = D3DCompileFromFile(file,
                            NULL,
                            D3D_COMPILE_STANDARD_FILE_INCLUDE,
                            "PS",
                            "ps_5_1",
                            D3DCOMPILE_SKIP_OPTIMIZATION | D3DCOMPILE_DEBUG,
                            0,
                            ps_blob,
                            &error_blob);
    if (error_blob)
    {
        char *error_msg = (char *)error_blob->GetBufferPointer();
        OutputDebugStringA(error_msg);
    }
    ASSERT(SUCCEEDED(hr));
}

void fill_packed_resource(
    size_t vertex_size, size_t vertex_count, quad_vert2d *vertices,
    size_t index_size, size_t index_count, WORD *indices,
    size_t heap_index)
{
    size_t vertices_size = vertex_size * vertex_count;
    size_t indices_size = index_size * index_count;

    size_t vertices_offset = (vertices_size + indices_size) * heap_index;
    size_t indices_offset = vertices_size * (heap_index + 1) + (heap_index * indices_size);

    memcpy((void *)&cpu_mapped_packed[vertices_offset],
           (void *)vertices,
           vertices_size);

    memcpy((void *)&cpu_mapped_packed[indices_offset],
           (void *)indices,
           indices_size);

    quad new_quad = {};
    new_quad.ibv.BufferLocation = packed_default_resource->GetGPUVirtualAddress() + indices_offset;
    new_quad.ibv.Format = DXGI_FORMAT::DXGI_FORMAT_R16_UINT;
    new_quad.ibv.SizeInBytes = (UINT)indices_size;

    new_quad.vbv.BufferLocation = packed_default_resource->GetGPUVirtualAddress() + vertices_offset;
    new_quad.vbv.SizeInBytes = (UINT)vertices_size;
    new_quad.vbv.StrideInBytes = (UINT)vertex_size;

    quads.push_back(new_quad);
}

void create_quads(int count)
{
    if (count <= 0)
        return;

    hr = ui_requests_cmd_alloc->Reset();
    ASSERT(SUCCEEDED(hr));
    hr = ui_requests_cmdlist->Reset(ui_requests_cmd_alloc, nullptr);
    ASSERT(SUCCEEDED(hr));

    for (int i = 0; i < count; ++i)
    {
        quad_vert2d vertices[4] = {
            {{-0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}}, // 0
            {{-0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}},  // 1
            {{0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}},   // 2
            {{0.5f, -0.5f}, {0.0f, 0.0f, 1.0f}},  // 3
        };

        WORD indices[6] = {
            0, 1, 2,
            3, 0, 2};

        fill_packed_resource(sizeof(quad_vert2d), _countof(vertices), vertices,
                             sizeof(WORD), _countof(indices), indices, i);

        quads[i].cb_index = (max_quads - i) - 1;
        append_cb_toheap((int)quads[i].cb_index, quads[i].cb_index);

        XMMATRIX scale = XMMatrixScaling(0.4f, 0.4f, 0.4f);
        XMMATRIX translation = XMMatrixTranslation(0.f, (float)i * 1.f, 0.f);
        XMMATRIX model = translation * scale;
        XMMATRIX t_model = XMMatrixTranspose(model);
        memcpy((void *)&cpu_map_prealloc_modelcb[quads[i].cb_index * model_cb_size], (void *)&t_model, sizeof(XMMATRIX));

        XMVECTOR tv = translation.r[3];
        quads[i].position.x = XMVectorGetX(tv);
        quads[i].position.y = XMVectorGetY(tv);

        // loop over all vertices to find the min and max vector
        XMVECTOR vmax = XMVectorReplicate(-FLT_MAX);
        XMVECTOR vmin = XMVectorReplicate(+FLT_MAX);
        for (int i = 0; i < _countof(vertices); i++)
        {
            XMVECTOR p = XMVectorSet(vertices[i].position[0], vertices[i].position[1], 0.f, 0.f);
            XMVECTOR pm = XMVector3Transform(p, model);
            vmin = XMVectorMin(vmin, pm);
            vmax = XMVectorMax(vmax, pm);
        }

        quads[i].bounds.CreateFromPoints(quads[i].bounds,
                                         XMVectorSet(XMVectorGetX(vmin), XMVectorGetY(vmin), 0.f, 0.f),
                                         XMVectorSet(XMVectorGetX(vmax), XMVectorGetY(vmax), 0.f, 0.f));
    }

    ui_requests_cmdlist->CopyBufferRegion(
        packed_default_resource, 0,
        packed_uploader, 0, aligned_packed_uploadheap_size);

    ui_requests_cmdlist->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
                                                packed_default_resource,
                                                D3D12_RESOURCE_STATE_COPY_DEST,
                                                D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER | D3D12_RESOURCE_STATE_INDEX_BUFFER));

    ui_requests_cmdlist->Close();
    g_cmd_queue->ExecuteCommandLists(1, (ID3D12CommandList *const *)&ui_requests_cmdlist);
}

void create_cb_resource_per_cbv(int triangle_index, size_t heap_index)
{
    int i = triangle_index;

    hr = g_device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE::D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAGS::D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(model_cb_size),
        D3D12_RESOURCE_STATES::D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr, __uuidof(ID3D12Resource), (void **)&triangles[i].model_cb_resource);
    wchar_t name_buf[100];
    swprintf_s(name_buf, 100, L"%s%d", L"model_cb_resource_", i);
    triangles[i].model_cb_resource->SetName(name_buf);
    ASSERT(SUCCEEDED(hr));

    D3D12_RANGE range = {};
    hr = triangles[i].model_cb_resource->Map(0, &range, (void **)&triangles[i].cpu_mapped_model_cb);
    ASSERT(SUCCEEDED(hr));

    XMMATRIX scale = XMMatrixScaling(0.2f, 0.2f, 0.2f);
    XMMATRIX translation = XMMatrixTranslation((float)i * 1.0f, 0.0f, 0.0f);
    XMMATRIX t_model = XMMatrixTranspose(translation * scale);

    memcpy((void *)triangles[i].cpu_mapped_model_cb, (void *)&t_model, sizeof(XMMATRIX));
    triangles[i].model_cb_resource->Unmap(0, &range);

    D3D12_CPU_DESCRIPTOR_HANDLE handle = {};
    handle.ptr = cbv_srv_uav_heap->GetCPUDescriptorHandleForHeapStart().ptr + (heap_index * srv_desc_handle_incr_size);

    D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_model_desc;
    cbv_model_desc.BufferLocation = triangles[i].model_cb_resource->GetGPUVirtualAddress();
    cbv_model_desc.SizeInBytes = model_cb_size;
    g_device->CreateConstantBufferView(&cbv_model_desc, handle);
}

void append_cb_toheap(int triangle_index, size_t heap_index)
{
    if (!is_resource_created)
    {
        hr = g_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE::D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAGS::D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(model_cb_size * max_tris),
            D3D12_RESOURCE_STATES::D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr, __uuidof(ID3D12Resource), (void **)&preallocated_modelcb_resource);
        preallocated_modelcb_resource->SetName(L"preallocated_modelcb_resource");
        ASSERT(SUCCEEDED(hr));

        D3D12_RANGE range = {};
        hr = preallocated_modelcb_resource->Map(0, &range, (void **)&cpu_map_prealloc_modelcb);
        ASSERT(SUCCEEDED(hr));

        is_resource_created = true;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE handle = {};
    handle.ptr = cbv_srv_uav_heap->GetCPUDescriptorHandleForHeapStart().ptr + (heap_index * srv_desc_handle_incr_size);

    D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_model_desc;
    cbv_model_desc.BufferLocation = preallocated_modelcb_resource->GetGPUVirtualAddress() + (triangle_index * model_cb_size);
    cbv_model_desc.SizeInBytes = model_cb_size;
    g_device->CreateConstantBufferView(&cbv_model_desc, handle);
}

void create_cb_resource_per_frame(int triangle_index)
{
    int i = triangle_index;

    XMMATRIX scale = XMMatrixScaling(0.2f, 0.2f, 0.2f);
    XMMATRIX translation = XMMatrixTranslation(XMScalarSinEst((float)i * 1.0f), XMScalarSinEst((float)i * 1.0f), 0.0f);
    XMMATRIX t_model = XMMatrixTranspose(translation * scale);

    frame_context *frame_ctx = &g_frame_context[backbuffer_index];
    UINT64 current_triangle_index = frame_ctx->triangle_index;
    UINT64 copy_offset = current_triangle_index * model_cb_size;

    memcpy((void *)&frame_ctx->cpu_mapped_model_cb[copy_offset], // i needs to be an index belonging to each frame context
           (void *)&t_model, sizeof(XMMATRIX));

    D3D12_CPU_DESCRIPTOR_HANDLE handle = {};
    handle.ptr = cbv_srv_uav_heap->GetCPUDescriptorHandleForHeapStart().ptr + (i * srv_desc_handle_incr_size);

    D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_model_desc;
    cbv_model_desc.BufferLocation = g_frame_context[backbuffer_index].modelcb_resource->GetGPUVirtualAddress() + copy_offset;
    cbv_model_desc.SizeInBytes = model_cb_size;
    g_device->CreateConstantBufferView(&cbv_model_desc, handle);

    current_triangle_index++;
    frame_ctx->triangle_index = current_triangle_index;
}

void resize_swapchain(HWND hWnd, int width, int height)
{
    DXGI_SWAP_CHAIN_DESC1 sd;
    g_swapchain->GetDesc1(&sd);
    sd.Width = (UINT)width;
    sd.Height = (UINT)height;

    IDXGIFactory4 *dxgiFactory = NULL;
    g_swapchain->GetParent(__uuidof(IDXGIFactory3), (void **)&dxgiFactory);

    g_swapchain->Release();
    CloseHandle(g_hswapchain_waitableobject);

    IDXGISwapChain1 *swap_chain = NULL;

    dxgiFactory->CreateSwapChainForHwnd(
        (IUnknown *)g_cmd_queue,
        hWnd,
        &sd,
        NULL,
        NULL,
        &swap_chain);
    swap_chain->QueryInterface(__uuidof(IDXGISwapChain1), (void **)&g_swapchain);
    swap_chain->Release();
    dxgiFactory->Release();

    g_swapchain->SetMaximumFrameLatency(NUM_BACK_BUFFERS);

    g_hswapchain_waitableobject =
        g_swapchain->GetFrameLatencyWaitableObject();
    assert(g_hswapchain_waitableobject != NULL);
}

extern "C" __declspec(dllexport) bool update_and_render()
{
    frame_context *frame_ctx = WaitForNextFrameResources();
    frame_ctx->cmd_alloc->Reset();
    g_cmd_list->Reset(frame_ctx->cmd_alloc, NULL);
    cmd_lists.push_back(g_cmd_list);

    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    if (show_demo_window)
        ImGui::ShowDemoWindow(&show_demo_window);

    {
        imgui_app_combo();
        imgui_mouse_pos();
        imgui_gpu_memory(adapter);

        ImGuiContext *imguictx = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(imguictx);
        ImGui::Checkbox("Demo Window", &show_demo_window);
        ImGui::Checkbox("VSync", &is_vsync);

        ImGui::Text("frame gpu time %.4f ms/frame",
                    frame_gpu_time);

        ImGui::Text("imgui gpu time %.4f ms/frame",
                    imgui_gpu_time);

        ImGui::Text("triangles gpu time %.4f ms/frame",
                    triangle_gpu_time);

        ImGui::Text("quads gpu time %.4f ms/frame",
                    quads_gpu_time);

        ImGui::Text("quads and triangles gpu time %.4f ms/frame",
                    quads_and_tri_gpu_time);

        ImGui::Text("elapsed time %.4f ms/frame",
                    delta_time.elapsed_ms);

        ImGui::Text("average delta %.4f ms/frame",
                    delta_time_avg);

        ImGui::Text("Application average %.4f ms/frame (%.1f FPS)",
                    (double)(1000.0f / ImGui::GetIO().Framerate),
                    (double)ImGui::GetIO().Framerate);

        ImGui::Separator();

        const char *cbv_creation_str[] = {"Committed resource per CBV",
                                          "Committed resource per frame",
                                          "Multiple CBVs per committed resource"};
        static const char *current_technique = cbv_creation_str[0];
        if (ImGui::BeginCombo("CBV creation options", current_technique))
        {
            for (int i = 0; i < _countof(cbv_creation_str); ++i)
            {
                bool is_selected = (current_technique == cbv_creation_str[i]);
                if (ImGui::Selectable(cbv_creation_str[i], is_selected))
                {
                    current_technique = cbv_creation_str[i];
                    cbv_creation_option = (cbv_creation_options)i;
                }

                if (is_selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        if (ImGui::InputInt("Triangle(s)", &total_tris_torender_input))
        {
            if (total_tris_torender_input >= 0 && total_tris_torender_input <= max_tris)
            {
                total_tris_torender = total_tris_torender_input;
                num_missing_tris = total_tris_torender - num_tris_rendered;
            }
        }
        ImGui::SameLine();
        char buf_tricount[50];
        sprintf(buf_tricount, "%lld / %d", triangles.size(), max_tris);
        ImGui::Text(buf_tricount);

        if (ImGui::InputInt("Instance count", &tri_instance_count))
        {
        }

        ImGui::Separator();

        if (ImGui::InputInt("Quad(s)", &total_quads_torender, 0, 0, 0))
        {
            if (total_quads_torender <= max_quads)
            {
                if (!quads.empty())
                {
                    quads.clear();
                }
                create_quads(total_quads_torender);
            }
        }
        ImGui::SameLine();
        char buf_handlecount[50];
        sprintf(buf_handlecount, "%lld / %d", quads.size(), max_quads);
        ImGui::Text(buf_handlecount);
    }

    //render

    queries->start("frame_rendering");

    D3D12_RECT rect;
    rect.left = 0;
    rect.top = 0;
    rect.right = (LONG)g_hwnd_width;
    rect.bottom = (LONG)g_hwnd_height;
    g_cmd_list->RSSetScissorRects(1, &rect);

    D3D12_VIEWPORT vp;
    vp.TopLeftY = 0;
    vp.TopLeftX = 0;
    vp.Width = (float)g_hwnd_width;
    vp.Height = (float)g_hwnd_height;
    vp.MaxDepth = 1.0f;
    vp.MinDepth = 0.0f;
    g_cmd_list->RSSetViewports(1, &vp);

    backbuffer_index = g_swapchain->GetCurrentBackBufferIndex(); // gets updated after each call to Present()
    next_backbuffer_index = (backbuffer_index + 1) % NUM_BACK_BUFFERS;

    g_cmd_list->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
                                       g_main_rt_resources[backbuffer_index],
                                       D3D12_RESOURCE_STATES::D3D12_RESOURCE_STATE_PRESENT,
                                       D3D12_RESOURCE_STATES::D3D12_RESOURCE_STATE_RENDER_TARGET));

    g_cmd_list->ClearDepthStencilView(dsv_heap->GetCPUDescriptorHandleForHeapStart(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.f, 0, 0, NULL);
    g_cmd_list->ClearRenderTargetView(g_rtv_descriptors[backbuffer_index], Colors::BlanchedAlmond, 0, NULL);

    g_cmd_list->OMSetRenderTargets(1, &g_rtv_descriptors[backbuffer_index], FALSE, &dsv_heap->GetCPUDescriptorHandleForHeapStart());

    g_cmd_list->SetDescriptorHeaps(1, &cbv_srv_uav_heap);
    g_cmd_list->SetGraphicsRootSignature(g_rootsig);

    // allocate and upload triangles data
    g_cmd_list->IASetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY::D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    if (num_missing_tris > 0)
    {
        for (int i = num_tris_rendered; num_missing_tris > 0; ++i)
        {
            triangle_mesh new_tri;
            triangles.push_back(new_tri);

            // committed resource per triangle
            triangle_data vertices[triangle_vertices_count] = {
                {{0.0f, 0.25f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
                {{0.25f, -0.25f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},
                {{-0.25f, -0.25f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f, 1.0f}},
            };

            size_t stride = sizeof(triangle_data);
            size_t vertex_buffer_byte_size = stride * _countof(vertices);

            hr = g_device->CreateCommittedResource(
                &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE::D3D12_HEAP_TYPE_DEFAULT),
                D3D12_HEAP_FLAGS::D3D12_HEAP_FLAG_NONE,
                &CD3DX12_RESOURCE_DESC::Buffer(vertex_buffer_byte_size),
                D3D12_RESOURCE_STATES::D3D12_RESOURCE_STATE_COPY_DEST,
                NULL, __uuidof(ID3D12Resource), (void **)&triangles[i].vertex_default_resource);
            ASSERT(SUCCEEDED(hr));
            triangles[i].vertex_default_resource->SetName(L"vertex_default_resource");

            hr = g_device->CreateCommittedResource(
                &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE::D3D12_HEAP_TYPE_UPLOAD),
                D3D12_HEAP_FLAGS::D3D12_HEAP_FLAG_NONE,
                &CD3DX12_RESOURCE_DESC::Buffer(vertex_buffer_byte_size),
                D3D12_RESOURCE_STATES::D3D12_RESOURCE_STATE_GENERIC_READ,
                NULL, __uuidof(ID3D12Resource), (void **)&triangles[i].vertex_upload_resource);
            ASSERT(SUCCEEDED(hr));
            triangles[i].vertex_upload_resource->SetName(L"vertex_upload_resource");

            BYTE *mapped_vertex_data = NULL;
            D3D12_RANGE range = {};
            triangles[i].vertex_upload_resource->Map(0, &range, (void **)&mapped_vertex_data);
            memcpy((void *)mapped_vertex_data,
                   (void *)vertices,
                   vertex_buffer_byte_size);

            triangles[i].vertex_upload_resource->Unmap(0, &range);

            g_cmd_list->CopyBufferRegion(
                triangles[i].vertex_default_resource, 0,
                triangles[i].vertex_upload_resource, 0,
                vertex_buffer_byte_size);

            triangles[i].vbv.BufferLocation = triangles[i].vertex_default_resource->GetGPUVirtualAddress();
            triangles[i].vbv.SizeInBytes = (UINT)vertex_buffer_byte_size;
            triangles[i].vbv.StrideInBytes = (UINT)stride;

            g_cmd_list->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
                                               triangles[i].vertex_default_resource,
                                               D3D12_RESOURCE_STATES::D3D12_RESOURCE_STATE_COPY_DEST,
                                               D3D12_RESOURCE_STATES::D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER));

            triangles[i].cb_index = num_tris_rendered;

            // create CBVs
            switch (cbv_creation_option)
            {
            case committed_resource_per_cbv:
                create_cb_resource_per_cbv(i, triangles[i].cb_index);
                break;

            case committed_resource_per_frame:
                create_cb_resource_per_frame(i);
                break;

            case committed_resource_multiple_cbv:
                append_cb_toheap(i, triangles[i].cb_index);
                XMMATRIX scale = XMMatrixScaling(0.2f, 0.2f, 0.2f);
                XMMATRIX translation = XMMatrixTranslation(0.f, (float)i * 1.f, 0.f);
                XMMATRIX model = translation * scale;
                XMMATRIX t_model = XMMatrixTranspose(model);
                memcpy((void *)&cpu_map_prealloc_modelcb[i * model_cb_size], (void *)&t_model, sizeof(XMMATRIX));
                break;

            default:
                break;
            }

            num_tris_rendered++;
            num_missing_tris--;
        }
    }

    // deallocate unused triangles
    int tris_to_delete = (int)triangles.size() - total_tris_torender;
    if (tris_to_delete > 0 && !triangles.empty())
    {
        for (int i = 0; i < tris_to_delete; ++i)
        {
            triangle_mesh tri_to_delete = triangles.back();
            cpu_wait(g_fence_last_signaled_value);
            safe_release(tri_to_delete.model_cb_resource);
            safe_release(tri_to_delete.vertex_default_resource);
            safe_release(tri_to_delete.vertex_upload_resource);

            triangles.pop_back();
            num_tris_rendered--;
        }
    }

    queries->start("triangle_rendering");
    queries->start("quads_and_tri");
    // draw triangles
    for (int i = 0; i < total_tris_torender; ++i)
    {
        D3D12_GPU_DESCRIPTOR_HANDLE handle = {};
        handle.ptr = cbv_srv_uav_heap->GetGPUDescriptorHandleForHeapStart().ptr + (triangles[i].cb_index * srv_desc_handle_incr_size);
        g_cmd_list->SetGraphicsRootDescriptorTable(1, handle);

        g_cmd_list->SetPipelineState(g_pso);
        g_cmd_list->IASetVertexBuffers(0, 1, &triangles[i].vbv);
        g_cmd_list->DrawInstanced(3, tri_instance_count, 0, 0);
    }
    queries->stop("triangle_rendering");

    queries->start("quads_rendering");
    // draw quads
    for (int i = 0; i < quads.size(); i++)
    {
        D3D12_GPU_DESCRIPTOR_HANDLE handle = {};
        handle.ptr = cbv_srv_uav_heap->GetGPUDescriptorHandleForHeapStart().ptr + (quads[i].cb_index * srv_desc_handle_incr_size);
        g_cmd_list->SetGraphicsRootDescriptorTable(1, handle);
        g_cmd_list->IASetVertexBuffers(0, 1, &quads[i].vbv);
        g_cmd_list->IASetIndexBuffer(&quads[i].ibv);

        if (quads[i].bounds.Contains(XMVectorSet(ndc_mouse_pos.x, ndc_mouse_pos.y, 0.f, 0.f)))
        {
            // draw to stencil buffer
            g_cmd_list->OMSetStencilRef(1);
            g_cmd_list->SetPipelineState(stencil_pso);
            g_cmd_list->DrawIndexedInstanced(6, 1, 0, 0, 0);

            // draw scaled up outline
            g_cmd_list->SetPipelineState(outline_pso);
            g_cmd_list->DrawIndexedInstanced(6, 1, 0, 0, 0);
        }

        // draw quad
        g_cmd_list->SetPipelineState(quad_pso);
        g_cmd_list->DrawIndexedInstanced(6, 1, 0, 0, 0);
    }
    queries->stop("quads_and_tri");
    queries->stop("quads_rendering");

    queries->start("ui_rendering");

    PIXBeginEvent(g_cmd_list, 0, "imgui_render");
    imgui_render(g_cmd_list);
    PIXEndEvent();

    queries->stop("ui_rendering");
    queries->stop("frame_rendering");
    queries->resolve();

    g_cmd_list->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
                                       g_main_rt_resources[backbuffer_index],
                                       D3D12_RESOURCE_STATES::D3D12_RESOURCE_STATE_RENDER_TARGET,
                                       D3D12_RESOURCE_STATES::D3D12_RESOURCE_STATE_PRESENT));

    g_cmd_list->Close();
    g_cmd_queue->ExecuteCommandLists((UINT)cmd_lists.size(), (ID3D12CommandList *const *)cmd_lists.data());
    cmd_lists.clear();

    imgui_gpu_time = queries->result("ui_rendering");
    triangle_gpu_time = queries->result("triangle_rendering");
    quads_and_tri_gpu_time = queries->result("quads_and_tri");
    quads_gpu_time = queries->result("quads_rendering");
    frame_gpu_time = queries->result("frame_rendering");

    UINT sync_interval = is_vsync ? 1 : 0;
    UINT present_flags = is_vsync ? 0 : DXGI_PRESENT_ALLOW_TEARING;

    g_swapchain->Present(sync_interval, present_flags);

    UINT64 fence_value = g_fence_last_signaled_value + 1;
    g_cmd_queue->Signal(g_fence, fence_value);
    g_fence_last_signaled_value = fence_value;
    frame_ctx->fence_value = fence_value;

    DXGI_FRAME_STATISTICS frame_stats;
    g_swapchain->GetFrameStatistics(&frame_stats);

    LARGE_INTEGER current_time;
    QueryPerformanceCounter(&current_time);
    delta_time.end_time = (double)current_time.QuadPart;

    delta_time.elapsed_ms = (delta_time.end_time - delta_time.start_time);
    delta_time.elapsed_ms /= g_cpu_frequency;
    delta_time.elapsed_ms *= millisecond;

    delta_time.start_time = delta_time.end_time;

    delta_times[stats_counter++] = delta_time.elapsed_ms;
    if (stats_counter == BUFFERED_FRAME_STATS)
        stats_counter = 0;

    return true;
}

extern "C" __declspec(dllexport) void resize(int width, int height)
{
    g_hwnd_width = width;
    g_hwnd_height = height;

    ImGui_ImplDX12_InvalidateDeviceObjects();
    cleanup_rendertarget();
    safe_release(dsv_resource);

    resize_swapchain(g_hwnd, width, height);
    create_dsv(width, height);
    create_rendertarget();
    ImGui_ImplDX12_CreateDeviceObjects();
}

void cleanup_device()
{
    delete queries;
    cleanup_rendertarget();

    if (g_hswapchain_waitableobject != NULL)
        CloseHandle(g_hswapchain_waitableobject);

    if (g_fence_event)
    {
        CloseHandle(g_fence_event);
        g_fence_event = NULL;
    }

    for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
    {
        safe_release(g_frame_context[i].cmd_alloc);
        safe_release(g_frame_context[i].modelcb_resource);
    }

    for (int i = 0; triangles.size() > i; ++i)
    {
        safe_release(triangles[i].vertex_default_resource);
        safe_release(triangles[i].vertex_upload_resource);
        safe_release(triangles[i].model_cb_resource);
    }

    for (int i = 0; i < _countof(g_main_rt_resources); ++i)
    {
        safe_release(g_main_rt_resources[i]);
    }

    safe_release(g_swapchain);
    safe_release(packed_default_resource);
    safe_release(packed_uploader);
    safe_release(ui_requests_cmdlist);
    safe_release(ui_requests_cmd_alloc);
    safe_release(g_cmd_queue);
    safe_release(g_cmd_list);
    safe_release(rtv_desc_heap);
    safe_release(cbv_srv_uav_heap);
    safe_release(dsv_heap);
    safe_release(g_fence);
    safe_release(query_rb_buffer);
    safe_release(query_heap);
    safe_release(g_pso);
    safe_release(quad_pso);
    safe_release(stencil_pso);
    safe_release(outline_pso);
    safe_release(g_rootsig);
    safe_release(dsv_resource);
    safe_release(preallocated_modelcb_resource);
    safe_release(quad_vs_blob);
    safe_release(quad_ps_blob);
    safe_release(tri_vs_blob);
    safe_release(tri_ps_blob);
    safe_release(adapter);
    safe_release(g_device);

#ifdef DX12_ENABLE_DEBUG_LAYER
    IDXGIDebug1 *pDebug = NULL;
    if (SUCCEEDED(DXGIGetDebugInterface1(0, __uuidof(IDXGIDebug1), (void **)&pDebug)))
    {
        pDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_DETAIL);
        pDebug->Release();
    }
#endif
}
