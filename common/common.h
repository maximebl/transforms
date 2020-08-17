#pragma once
#include "pch.h"

#define s_internal static

#ifdef COMMON_EXPORTS
#define COMMON_API __declspec(dllexport)
#else
#define COMMON_API __declspec(dllimport)
#endif

extern COMMON_API HRESULT hr;
extern COMMON_API HWND g_hwnd;
extern COMMON_API UINT64 g_hwnd_width;
extern COMMON_API UINT g_hwnd_height;

extern COMMON_API wchar_t gamecodedll_path[MAX_PATH];
extern COMMON_API wchar_t tempgamecodedll_path[MAX_PATH];
extern COMMON_API wchar_t win32_exe_location[MAX_PATH];
extern COMMON_API wchar_t gamecodedll_name[MAX_PATH];
extern COMMON_API wchar_t temp_gamecodedll_name[MAX_PATH];

enum demos
{
    transforms2d = 0,
    transforms3d = 1,
    particles = 2,
};
extern COMMON_API demos demo_to_show;
extern COMMON_API bool demo_changed;

COMMON_API void set_dll_paths(const wchar_t *path);
COMMON_API void failed_assert(const char *file, int line, const char *statement);
COMMON_API std::string last_error();

#define ASSERT(b) \
    if (!(b))     \
    failed_assert(__FILE__, __LINE__, #b)

#define safe_release(p)     \
    do                      \
    {                       \
        if (p)              \
        {                   \
            (p)->Release(); \
            (p) = NULL;     \
        }                   \
    } while ((void)0, 0)
