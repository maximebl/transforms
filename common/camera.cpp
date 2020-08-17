#include "pch.h"
#include "camera.h"

using namespace DirectX;

camera::camera(float aspect_ratio, float fov_angle, DirectX::FXMVECTOR position) : m_start_pos(position), position(position)
{
    proj = XMMatrixPerspectiveFovLH(fov_angle, aspect_ratio, 1.0f, 1000.0f);
    m_start_pos = position;

    right = DirectX::XMVectorSet(1.f, 0.f, 0.f, 0.f);
    up = DirectX::XMVectorSet(0.f, 1.f, 0.f, 0.f);
    forward = DirectX::XMVectorSet(0.f, 0.f, 1.f, 0.f);

    XMVECTOR scaled_cam_dir = XMVectorScale(forward, 15.f);
    default_orbit_target = position + scaled_cam_dir;
    orbit_target_pos = default_orbit_target;
}

void camera::update_view()
{
    up = XMVector3Normalize(up);
    forward = XMVector3Normalize(forward);
    right = XMVector3Cross(up, forward);
    up = XMVector3Normalize(XMVector3Cross(forward, right));

    // Camera translation
    float x = -XMVectorGetX(XMVector3Dot(right, position));
    float y = -XMVectorGetX(XMVector3Dot(up, position));
    float z = -XMVectorGetX(XMVector3Dot(forward, position));

    XMMATRIX T;
    T.r[0] = right;
    T.r[0].m128_f32[3] = x;

    T.r[1] = up;
    T.r[1].m128_f32[3] = y;

    T.r[2] = forward;
    T.r[2].m128_f32[3] = z;

    T.r[3] = XMVectorSet(0.f, 0.f, 0.f, 1.f);

    view = T;
}

void camera::update_yaw_pitch(XMFLOAT2 current_mouse_pos, XMFLOAT2 last_mouse_pos)
{
    float dx = XMConvertToRadians(0.5f * (current_mouse_pos.x - last_mouse_pos.x));
    float dy = XMConvertToRadians(0.5f * (current_mouse_pos.y - last_mouse_pos.y));

    if (GetAsyncKeyState(VK_MENU))
    {
        XMVECTOR start_cam_pos = position;

        // Get the direction vector from the camera position to the target position (position - point = direction)
        XMVECTOR cam_to_target_dir = orbit_target_pos - position;

        // Translate to target position (direction + point = point)
        position = cam_to_target_dir + position;

        XMMATRIX to_yaw = XMMatrixRotationY(dx);
        XMMATRIX to_pitch = XMMatrixRotationAxis(right, dy);
        XMMATRIX to_rot = to_pitch * to_yaw; // apply global yaw to local pitch

        XMVECTOR rot_cam_forward;
        rot_cam_forward = XMVector3Transform(forward, to_rot);
        right = XMVector3Transform(right, to_rot);
        up = XMVector3Transform(up, to_rot);

        rot_cam_forward = XMVector3Normalize(rot_cam_forward);

        // get the distance from the camera position to the object it orbits
        XMVECTOR cam_to_target = XMVectorSubtract(orbit_target_pos, start_cam_pos);
        float dist_to_target = XMVectorGetX(XMVector3Length(cam_to_target));

        // Place the camera back
        rot_cam_forward = XMVectorScale(rot_cam_forward, -dist_to_target);
        position = XMVectorAdd(position, rot_cam_forward);

        //look at the thing we're orbiting around
        forward = XMVector4Normalize(XMVectorSubtract(orbit_target_pos, position));
    }
    else
    {
        XMMATRIX yaw = XMMatrixRotationY(dx);
        forward = XMVector3TransformNormal(forward, yaw);
        right = XMVector3TransformNormal(right, yaw);
        up = XMVector3TransformNormal(up, yaw);

        XMMATRIX pitch = XMMatrixRotationAxis(right, dy);
        forward = XMVector3TransformNormal(forward, pitch);
        up = XMVector3TransformNormal(up, pitch);
    }
}
