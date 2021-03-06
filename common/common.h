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
extern COMMON_API float g_aspect_ratio;

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
COMMON_API void failed_assert(const char *file, int line, const char *statement, std::string message = "No message provided.");
COMMON_API std::string last_error();
COMMON_API void wait_duration(DWORD duration);

COMMON_API void check_hr(HRESULT hr);
COMMON_API std::string hr_msg(HRESULT hr);

#define ASSERT2(statement, message) \
    if (!(statement))               \
    failed_assert(__FILE__, __LINE__, #statement, message)

#define ASSERT(statement) \
    if (!(statement))     \
    failed_assert(__FILE__, __LINE__, #statement)

#define safe_release(p)     \
    do                      \
    {                       \
        if (p)              \
        {                   \
            (p)->Release(); \
            (p) = NULL;     \
        }                   \
    } while ((void)0, 0)

COMMON_API inline size_t align_up(size_t value, size_t alignment)
{
    return ((value + (alignment - 1)) & ~(alignment - 1));
}
