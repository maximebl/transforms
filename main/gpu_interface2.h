#pragma once
#include <d3d12.h>
#include "../common/d3dx12.h"
#include "../common/common.h"

template <typename T>
struct upload_buffer2
{
    upload_buffer2(ID3D12Device *device,
                   size_t element_count,
                   bool is_constant_buffer = false)
    {
        m_element_byte_size = sizeof(T);
        if (is_constant_buffer)
            m_element_byte_size = align_up(m_element_byte_size, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);

        device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(m_element_byte_size * element_count),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_upload));

        m_upload->Map(0, nullptr, reinterpret_cast<void **>(&m_mapped_data));
    }

    ~upload_buffer2()
    {
        if (m_upload != nullptr)
            m_upload->Unmap(0, nullptr);
        m_mapped_data = nullptr;
        safe_release(m_upload);
    }

    void copy_data(size_t element_index, const T *data)
    {
        memcpy(&m_mapped_data[element_index * m_element_byte_size], data, sizeof(T));
    }

    ID3D12Resource *m_upload = nullptr;
    size_t m_element_byte_size = 0;
    BYTE *m_mapped_data = nullptr;
};

class gpu_interface2
{
};
