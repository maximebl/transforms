#pragma once
#include "common.h"
#include "gpu_interface.h"
#include <d3d12.h>
#include <unordered_map>
#include <vector>

class gpu_query
{
public:
    COMMON_API gpu_query(ID3D12Device *device, ID3D12GraphicsCommandList *cmd_list, ID3D12CommandQueue *cmd_queue, UINT *backbuffer_index, UINT num_queries);
    COMMON_API ~gpu_query();

    COMMON_API void start_query(const char *query_name);
    COMMON_API void end_query(const char *query_name);
    COMMON_API void resolve();
    COMMON_API double result(const char *query_name);

private:
    ID3D12GraphicsCommandList *m_cmd_list;
    ID3D12QueryHeap *m_query_heap;
    ID3D12Resource *m_query_rb_buffer;
    UINT m_timer_count;
    UINT64 *m_timestamp_buffer;
    double m_gpu_frequency;
    UINT *m_backbuffer_index;

    UINT m_num_queries;
    UINT m_queries_stride;
    struct query_info
    {
        UINT buffer_start;
        UINT buffer_end;
        UINT index;
    };
    std::unordered_map<const char *, query_info> m_queries;

};
