#pragma once
#include "common.h"
#include "transform.h"
#include "gpu_interface.h"

struct COMMON_API camera
{
    camera(transform in_transform = transform{},
           float in_near = 1.f,
           float in_far = 1000.f,
           float in_fov_angle = 0.25f * DirectX::XM_PI,
           float in_aspect_ratio = g_aspect_ratio);

    void update_position();
    void update_view();
    void calc_projection();
    void update_yaw_pitch(DirectX::XMFLOAT2 current_mouse_pos,
                          DirectX::XMFLOAT2 last_mouse_pos);
    std::vector<position_color> calc_frustum_vertices();
    std::vector<position_color> calc_plane_vertices(float dist_from_origin, DirectX::XMFLOAT4 color);
    void viewspace_frustum_planes();
    std::vector<position_color> calc_frustum_plane_vertices();
    bool plane_intersect(position_color* point, DirectX::XMFLOAT4 plane1, DirectX::XMFLOAT4 plane2, DirectX::XMFLOAT4 plane3);

    float m_vfov_angle = 0.f;
    float m_aspect_ratio = 0.f;
    float m_near = 0.f;
    float m_far = 0.f;
    float m_proj_dist = 0.f;
    transform m_transform = {};
    DirectX::XMFLOAT4X4 m_inv_view = {};
    DirectX::XMFLOAT4X4 m_proj = {};

    DirectX::XMFLOAT4 m_far_plane;
    DirectX::XMFLOAT4 m_near_plane;
    DirectX::XMFLOAT4 m_left_plane;
    DirectX::XMFLOAT4 m_right_plane;
    DirectX::XMFLOAT4 m_top_plane;
    DirectX::XMFLOAT4 m_bottom_plane;
};
