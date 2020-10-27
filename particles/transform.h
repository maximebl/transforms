#pragma once
#include "common.h"

struct transform
{
    transform() : m_translation(0.f, 0.f, 0.f),
                  m_rotation(0.f, 0.f, 0.f),
                  m_scale(1.f, 1.f, 1.f) {}

    void set_translation(float x, float y, float z)
    {
        m_translation.x = x;
        m_translation.y = y;
        m_translation.z = z;
        update_world();
    }

    void set_scale(float x, float y, float z)
    {
        m_scale.x = x;
        m_scale.y = y;
        m_scale.z = z;
        update_world();
    }

    void set_rotation(float x, float y, float z)
    {
        m_rotation.x = x;
        m_rotation.y = y;
        m_rotation.z = z;
        update_world();
    }

    void update_world()
    {
        using namespace DirectX;

        XMMATRIX transform;
        XMMATRIX scale = XMMatrixScaling(m_scale.x, m_scale.y, m_scale.z);
        XMMATRIX rotation = XMMatrixRotationX(m_rotation.x) * XMMatrixRotationY(m_rotation.y) * XMMatrixRotationZ(m_rotation.z);
        XMMATRIX translation = XMMatrixTranslation(m_translation.x, m_translation.y, m_translation.z);
        transform = (scale * rotation) * translation;
        XMStoreFloat4x4(&m_world, transform);
        XMStoreFloat4x4(&m_world_transposed, XMMatrixTranspose(transform));
    }

    DirectX::XMFLOAT3 m_translation;
    DirectX::XMFLOAT3 m_rotation;
    DirectX::XMFLOAT3 m_scale;
    DirectX::XMFLOAT4X4 m_world;
    DirectX::XMFLOAT4X4 m_world_transposed;
};
