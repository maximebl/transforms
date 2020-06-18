#pragma once
#include "pch.h"

class camera
{
public:
    DirectX::XMVECTOR right;
    DirectX::XMVECTOR up;
    DirectX::XMVECTOR forward;

    DirectX::XMVECTOR position = DirectX::XMVectorSet(0.f, 0.f, -10.f, 1.f);

    float roll = 0.f;
    float pitch = 0.f;
    float yaw = 0.f;

    void update_view();
};
