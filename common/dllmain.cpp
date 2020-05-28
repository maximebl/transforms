#include "pch.h"
#include "common.h"

COMMON_API HWND g_hwnd;
COMMON_API UINT64 g_hwnd_width;
COMMON_API UINT g_hwnd_height;
COMMON_API wchar_t gamecodedll_path[MAX_PATH];
COMMON_API wchar_t tempgamecodedll_path[MAX_PATH];
COMMON_API wchar_t win32_exe_location[MAX_PATH];
COMMON_API wchar_t gamecodedll_name[MAX_PATH] = L"\\transforms.dll";
COMMON_API wchar_t temp_gamecodedll_name[MAX_PATH] = L"\\temp_transforms.dll";
COMMON_API demos demo_to_show;
COMMON_API bool demo_changed;
COMMON_API void get_dll_path();
COMMON_API void set_dll_path(const wchar_t* path);
