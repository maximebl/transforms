#pragma once
#include "pch.h"
#include "common.h"

struct COMMON_API camera
{
    camera(float aspect_ratio, float fov_angle, DirectX::FXMVECTOR position);

    DirectX::XMVECTOR right = {};
    DirectX::XMVECTOR up = {};
    DirectX::XMVECTOR forward = {};
    DirectX::XMVECTOR m_start_pos = {};
    DirectX::XMVECTOR position = {};
    DirectX::XMVECTOR default_orbit_target = {};
    DirectX::XMVECTOR orbit_target_pos = {};

    DirectX::XMMATRIX view = {};
    DirectX::XMMATRIX proj = {};

    float roll = 0.f;
    float pitch = 0.f;
    float yaw = 0.f;

     float dx_angle = 0.f;
     float dy_angle = 0.f;

    void update_view();
    void update_yaw_pitch(DirectX::XMFLOAT2 current_mouse_pos, DirectX::XMFLOAT2 last_mouse_pos);
};
