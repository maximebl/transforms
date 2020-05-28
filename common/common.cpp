#include "common.h"
#include "stdlib.h"
#include "PathCch.h"

HRESULT hr = 0;

void get_dll_path()
{
    DWORD l_win32exe = GetModuleFileNameW(NULL, win32_exe_location, MAX_PATH);

    PathCchRemoveFileSpec(win32_exe_location, l_win32exe);

    wcscpy(gamecodedll_path, win32_exe_location);
    wcscat(gamecodedll_path, gamecodedll_name);

    wcscpy(tempgamecodedll_path, win32_exe_location);
    wcscat(tempgamecodedll_path, temp_gamecodedll_name);

    SetCurrentDirectoryW(win32_exe_location);
}

void set_dll_path(const wchar_t* path)
{
    wchar_t path_with_slashes[MAX_PATH];
    wcscpy(path_with_slashes, L"\\");
    wcscat(path_with_slashes, path);
    PathCchRemoveFileSpec(gamecodedll_path, MAX_PATH);
    wcscat(gamecodedll_path, path_with_slashes);

    wchar_t temp_prefix[MAX_PATH];
    wcscpy(temp_prefix, L"\\temp_");
    wcscat(temp_prefix, path);
    PathCchRemoveFileSpec(tempgamecodedll_path, MAX_PATH);
    wcscat(tempgamecodedll_path, temp_prefix);
}

void failed_assert(const char *file, int line, const char *statement)
{
    static bool debug = true;

    if (debug)
    {
        wchar_t str[1024];
        wchar_t message[1024];
        wchar_t wfile[1024];
        mbstowcs_s(NULL, message, statement, 1024);
        mbstowcs_s(NULL, wfile, file, 1024);
        wsprintfW(str, L"Failed: (%s)\n\nFile: %s\nLine: %d\n\n", message, wfile, line);

        if (IsDebuggerPresent())
        {
            wcscat_s(str, 1024, L"Debug?");
            int res = MessageBoxW(NULL, str, L"Assert failed", MB_YESNOCANCEL | MB_ICONERROR);
            if (res == IDYES)
            {
                __debugbreak();
            }
            else if (res == IDCANCEL)
            {
                debug = false;
            }
        }
        else
        {
            wcscat_s(str, 1024, L"Display more asserts?");
            if (MessageBoxW(NULL, str, L"Assert failed", MB_YESNO | MB_ICONERROR | MB_DEFBUTTON2) != IDYES)
            {
                debug = false;
            }
        }
    }
}
