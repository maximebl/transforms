#include "pch.h"
#include "common.h"

COMMON_API HWND g_hwnd;
COMMON_API UINT64 g_hwnd_width;
COMMON_API UINT g_hwnd_height;
COMMON_API float g_aspect_ratio;
COMMON_API wchar_t gamecodedll_path[MAX_PATH];
COMMON_API wchar_t tempgamecodedll_path[MAX_PATH];
COMMON_API wchar_t win32_exe_location[MAX_PATH];
COMMON_API wchar_t gamecodedll_name[MAX_PATH] = L"\\particles.dll";
COMMON_API wchar_t temp_gamecodedll_name[MAX_PATH] = L"\\temp_particles.dll";
COMMON_API demos demo_to_show = demos::particles;
COMMON_API bool demo_changed = true;
COMMON_API void set_dll_paths(const wchar_t* path);
