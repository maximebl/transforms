#include "pch.h"
#include "cameraz.h"
void camera::update_view()
{
    //ImGui::SliderFloat4("Camera position", cam.position.m128_f32, -150.f, 150.f);
    //ImGui::SliderFloat("Camera rotation", &cam.rot_y_angle, -180.f, +180.f);
    //ImGui::SliderFloat("Camera pitch", &cam.rot_pitch, -180.f, +180.f);
    ////XMMATRIX view = XMMatrixTranspose(view_matrix_lh(cam.position, world_up_dir, target_pos));

    //XMVECTOR camera_right = XMVectorSet(1.f, 0.f, 0.f, 0.f);
    //XMVECTOR camera_up = XMVectorSet(0.f, 1.f, 0.f, 0.f);
    //XMVECTOR camera_dir = XMVectorSet(0.f, 0.f, 1.f, 0.f);

    //// vector transpose(cam_rotation);
    //// create matrix from that vector
    //XMMATRIX rot_y = XMMatrixRotationY(XMConvertToRadians(cam.rot_y_angle));

    //camera_dir = XMVector3TransformNormal(camera_dir, rot_y);
    //camera_right = XMVector3TransformNormal(camera_right, rot_y);
    //camera_up = XMVector3TransformNormal(camera_up, rot_y);

    //camera_right = XMVector3Cross(camera_up, camera_dir);
    //camera_up = XMVector3Cross(camera_dir, camera_right);

    //// vector -cam_translation
    //// create matrix from that vector
    //float x = -XMVectorGetX(XMVector3Dot(camera_right, cam.position));
    //float y = -XMVectorGetX(XMVector3Dot(camera_up, cam.position));
    //float z = -XMVectorGetX(XMVector3Dot(camera_dir, cam.position));

    //XMFLOAT4X4 T;
    //T(0, 0) = camera_right.m128_f32[0];
    //T(1, 0) = camera_right.m128_f32[1];
    //T(2, 0) = camera_right.m128_f32[2];
    //T(3, 0) = x;

    //T(0, 1) = camera_up.m128_f32[0];
    //T(1, 1) = camera_up.m128_f32[1];
    //T(2, 1) = camera_up.m128_f32[2];
    //T(3, 1) = y;

    //T(0, 2) = camera_dir.m128_f32[0];
    //T(1, 2) = camera_dir.m128_f32[1];
    //T(2, 2) = camera_dir.m128_f32[2];
    //T(3, 2) = z;

    //T(0, 3) = 0.f;
    //T(1, 3) = 0.f;
    //T(2, 3) = 0.f;
    //T(3, 3) = 1.f;
    //XMMATRIX view = XMMatrixTranspose(XMLoadFloat4x4(&T));

    ////XMMATRIX view = XMMatrixTranspose(XMMatrixLookAtLH(cam.position, target_pos, world_up_dir));
    //XMStoreFloat4x4(&pass.view, view);

    //// projection matrix
    //float aspect_ratio = (float)g_hwnd_width / (float)g_hwnd_height;
    //XMMATRIX proj = XMMatrixTranspose(XMMatrixPerspectiveFovLH(0.25f * XM_PI, aspect_ratio, 1.0f, 1000.0f));
    //XMStoreFloat4x4(&pass.proj, proj);

    //frame->cb_passdata_upload->copy_data(0, (void *)&pass);
}
