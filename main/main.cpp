#define WIN32_LEAN_AND_MEAN
#include "Windows.h"
#include <tchar.h>

#include "../common/common.h"

typedef bool (*gamecode_initialize)(void);
typedef void (*gamecode_resize)(int width, int height);
typedef bool (*gamecode_update_and_render)(void);
typedef void (*gamecode_cleanup)(void);
typedef LRESULT (*gamecode_wndproc)(UINT msg, WPARAM wParam, LPARAM lParam);

struct game_code
{
    HMODULE game_dll;
    gamecode_resize resize;
    gamecode_wndproc wndproc;
    gamecode_initialize initialize;
    gamecode_update_and_render update_and_render;
    gamecode_cleanup cleanup;
    FILETIME last_dll_write;
    FILETIME source_dll_write;
};

static HMODULE win32code = NULL;
static bool game_is_ready = false;
static struct game_code gamecode;
static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static bool load_gamecode();
FILETIME file_last_write(wchar_t *filename);
bool hotreload();
static wchar_t window_text[100] = L"";

int main()
{
    set_dll_path(L"transforms.dll");
    get_dll_path();

    if (!load_gamecode())
        return 1;

    WNDCLASSEX wc = {sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0L, 0L, win32code, NULL, NULL, NULL, NULL, _T("transforms"), NULL};
    RegisterClassEx(&wc);
    g_hwnd = CreateWindow(wc.lpszClassName, _T("transforms"), WS_OVERLAPPEDWINDOW, 100, 100, 2560, 1600, NULL, NULL, wc.hInstance, NULL);

    ShowWindow(g_hwnd, SW_SHOWMAXIMIZED);
    UpdateWindow(g_hwnd);
    GetWindowTextW(g_hwnd, window_text, 100);

    RECT rect;
    if (GetClientRect(g_hwnd, &rect))
    {
        g_hwnd_width = rect.right - rect.left;
        g_hwnd_height = rect.bottom - rect.top;
    }

    if (!gamecode.initialize())
    {
        gamecode.cleanup();
        return 1;
    }

    game_is_ready = true;

    MSG msg;
    ZeroMemory(&msg, sizeof(msg));
    while (msg.message != WM_QUIT)
    {
        if (PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            continue;
        }

        if (demo_changed)
        {
            switch (demo_to_show)
            {
            case transforms2d:
                set_dll_path(L"transforms.dll");
                break;

            case transforms3d:
                set_dll_path(L"3d_transforms.dll");
                break;

            case particles:
                break;

            default:
                break;
            }
        }

        if (hotreload())
        {
            if (!load_gamecode())
                break;
            if (!gamecode.initialize())
                break;

            game_is_ready = true;
        }

        demo_changed = false;
        game_is_ready = gamecode.update_and_render();
    }

    gamecode.cleanup();
    if (gamecode.game_dll)
        FreeLibrary(gamecode.game_dll);
    DestroyWindow(g_hwnd);
    UnregisterClass(wc.lpszClassName, wc.hInstance);
    return 0;
}

bool load_gamecode()
{
    CopyFileW(gamecodedll_path, tempgamecodedll_path, 0);
    OutputDebugStringW(gamecodedll_path);

    gamecode.game_dll = LoadLibraryW(tempgamecodedll_path);
    gamecode.last_dll_write = file_last_write(gamecodedll_path);

    if (!gamecode.game_dll)
        return false;

    gamecode.resize = (gamecode_resize)GetProcAddress(gamecode.game_dll, "resize");
    gamecode.wndproc = (gamecode_wndproc)GetProcAddress(gamecode.game_dll, "wndproc");
    gamecode.initialize = (gamecode_initialize)GetProcAddress(gamecode.game_dll, "initialize");
    gamecode.update_and_render = (gamecode_update_and_render)GetProcAddress(gamecode.game_dll, "update_and_render");
    gamecode.cleanup = (gamecode_cleanup)GetProcAddress(gamecode.game_dll, "cleanup");

    if (!gamecode.resize || !gamecode.wndproc || !gamecode.initialize || !gamecode.update_and_render || !gamecode.cleanup)
        return false;

    return true;
}

FILETIME file_last_write(wchar_t *filename)
{
    FILETIME last_write_time = {0};
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (GetFileAttributesExW(filename, GetFileExInfoStandard, &data))
    {
        last_write_time = data.ftLastWriteTime;
    }
    return last_write_time;
}

bool hotreload()
{
    // This check for the last time the dll file was written to is the side effect that will trigger either:
    //	-	Hot reloading of the current dll (this happens when the dll is recompiled by the programmer)
    //	-	Swapping to an entirely different dll (this happens when a different demo is selected by the user of the app)
    gamecode.source_dll_write = file_last_write(gamecodedll_path);

    if (CompareFileTime(&gamecode.last_dll_write, &gamecode.source_dll_write) != 0)
    {
        game_is_ready = false;
        if (gamecode.game_dll)
        {
            gamecode.cleanup();
            FreeLibrary(gamecode.game_dll);
            gamecode.game_dll = NULL;
            gamecode.cleanup = NULL;
            gamecode.initialize = NULL;
            gamecode.resize = NULL;
            gamecode.update_and_render = NULL;
            gamecode.wndproc = NULL;
        }

        wchar_t hotreload_txtbuf[MAX_PATH] = L"";
        wchar_t prefix_hotreload_txtbuf[20] = L" - hot reloaded: ";
        if (demo_changed)
        {
            lstrcpyW(prefix_hotreload_txtbuf, L" - demo changed: ");
        }
        wchar_t time_hotreload_txtbuf[30] = L"";
        wcscat(hotreload_txtbuf, window_text);
        wcscat(hotreload_txtbuf, prefix_hotreload_txtbuf);
        SYSTEMTIME systime = {0};
        GetLocalTime(&systime);
        GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_FORCE24HOURFORMAT, &systime, NULL, time_hotreload_txtbuf, 30);
        wcscat(hotreload_txtbuf, time_hotreload_txtbuf);
        SetWindowTextW(g_hwnd, hotreload_txtbuf);

        return true;
    }
    return false;
}

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (game_is_ready)
    {
        gamecode.wndproc(msg, wParam, lParam);
    }

    switch (msg)
    {
    case WM_SIZE:
        if (game_is_ready && wParam != SIZE_MINIMIZED)
        {
            gamecode.resize(LOWORD(lParam), HIWORD(lParam));
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
            return 0;
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}
