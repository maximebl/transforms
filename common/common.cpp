#include "common.h"
#include "stdlib.h"
#include "PathCch.h"

HRESULT hr = 0;

void set_dll_paths(const wchar_t *path)
{
    DWORD l_win32exe = GetModuleFileNameW(NULL, win32_exe_location, MAX_PATH);
    PathCchRemoveFileSpec(win32_exe_location, l_win32exe);
    wcscpy(gamecodedll_path, win32_exe_location);
    wcscpy(tempgamecodedll_path, win32_exe_location);
    SetCurrentDirectoryW(win32_exe_location);

    wchar_t dll_name_with_slashes[MAX_PATH];
    wcscpy(dll_name_with_slashes, L"\\");
    wcscat(dll_name_with_slashes, path);
    wcscat(gamecodedll_path, dll_name_with_slashes);

    wchar_t temp_prefix[MAX_PATH];
    wcscpy(temp_prefix, L"\\temp_");
    wcscat(temp_prefix, path);
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

std::string last_error()
{
    char error_msg[1024];
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   NULL, GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                   error_msg, _countof(error_msg), NULL);
    return std::string(error_msg);
}
