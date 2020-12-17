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

s_internal HMODULE win32code = NULL;
s_internal bool game_is_ready = false;
s_internal struct game_code gamecode;
s_internal LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

s_internal bool load_gamecode();
FILETIME file_last_write(wchar_t *filename);
s_internal bool hotreload();
s_internal void update_window_titlebar();
s_internal bool set_high_priority_class();
s_internal wchar_t window_text[100] = L"";

int main()
{
    if (!set_high_priority_class())
        return false;

    set_dll_paths(L"particles.dll");
    demo_to_show = demos::particles;

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
        g_aspect_ratio = (float)g_hwnd_width / g_hwnd_height;
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
                set_dll_paths(L"transforms.dll");
                break;

            case transforms3d:
                set_dll_paths(L"3d_transforms.dll");
                break;

            case particles:
                set_dll_paths(L"particles.dll");
                break;

            default:
                set_dll_paths(L"particles.dll");
                break;
            }
        }

        if (hotreload())
        {
            update_window_titlebar();

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

        return true;
    }
    return false;
}

void update_window_titlebar()
{
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
}

s_internal bool set_high_priority_class()
{
    // Set process priority class to high for better performance
    if (!SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS))
    {
        char error_msg[1024];
        FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                       NULL, GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                       error_msg, _countof(error_msg), NULL);

        char buf[1024] = "Could not set the process priority class to high.\nReturned error code:\n";
        strcat(buf, error_msg);
        int selection = MessageBoxA(NULL, buf,
                                    "Info",
                                    MB_ABORTRETRYIGNORE | MB_ICONINFORMATION | MB_DEFBUTTON2);
        switch (selection)
        {
        case IDABORT:
            return false;

        case IDRETRY:
            set_high_priority_class();
            return false;

        case IDIGNORE:
            return true;

        default:
            return true;
        }
    }
    return true;
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
            g_aspect_ratio = (float)g_hwnd_width / g_hwnd_height;
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
