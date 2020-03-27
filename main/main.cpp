#define WIN32_LEAN_AND_MEAN
#include "Windows.h"
#include <tchar.h>
#include "PathCch.h"
#pragma comment(lib,"user32")
#pragma comment(lib, "Pathcch.lib")

typedef bool(*gamecode_initialize)(HWND* hwnd);
typedef void(*gamecode_resize)(HWND hWnd, int width, int height);
typedef bool(*gamecode_update_and_render)(void);
typedef void(*gamecode_cleanup)(void);
typedef LRESULT(*gamecode_wndproc)(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

struct game_code
{
	HMODULE game_dll ;
	gamecode_resize resize ;
	gamecode_wndproc wndproc ;
	gamecode_initialize initialize ;
	gamecode_update_and_render update_and_render ;
	gamecode_cleanup cleanup ;
	FILETIME last_dll_write ;
	FILETIME source_dll_write ;
};

static wchar_t gamecodedll_path[MAX_PATH];
static wchar_t tempgamecodedll_path[MAX_PATH];
static wchar_t win32_exe_location[MAX_PATH];
static wchar_t gamecodedll_name[MAX_PATH] = L"\\transforms.dll";
static wchar_t temp_gamecodedll_name[MAX_PATH] = L"\\temp_transforms.dll";
static HMODULE win32code = NULL;
static bool game_is_ready = false;
static struct game_code gamecode;
static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static void get_dll_path(void);
static bool load_gamecode(void);
FILETIME file_last_write(wchar_t* filename);
bool hotreload(void);
static wchar_t window_text[100] = L"";
static HWND hwnd;

int main(){
	get_dll_path();

	if (!load_gamecode())
		return 1;

	WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0L, 0L,win32code , NULL, NULL, NULL, NULL, _T("transforms"), NULL };
	RegisterClassEx(&wc);
	hwnd = CreateWindow(wc.lpszClassName, _T("transforms"), WS_OVERLAPPEDWINDOW, 100, 100, 1280, 800, NULL, NULL, wc.hInstance, NULL);

	ShowWindow(hwnd, SW_SHOWDEFAULT);
	UpdateWindow(hwnd);
	GetWindowTextW(hwnd, window_text, 100);

	if (!gamecode.initialize(&hwnd))
		return 1;

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

		if (hotreload())
		{
			if (!load_gamecode())
				return 1;
			if (!gamecode.initialize(&hwnd))
				return 1;
			game_is_ready = true;
		}

		game_is_ready = gamecode.update_and_render();
	}
	gamecode.cleanup();
	if (gamecode.game_dll)
		FreeLibrary(gamecode.game_dll);
	DestroyWindow(hwnd);
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

FILETIME file_last_write(wchar_t* filename)
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

		wchar_t hotreload_txtbuf[MAX_PATH] = L"";
		wchar_t prefix_hotreload_txtbuf[20] = L" - hot reloaded: ";
		wchar_t time_hotreload_txtbuf[30] = L"";
		lstrcatW(hotreload_txtbuf, window_text);
		lstrcatW(hotreload_txtbuf, prefix_hotreload_txtbuf);
		SYSTEMTIME systime = {0};
		GetLocalTime(&systime);
		GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_FORCE24HOURFORMAT, &systime, NULL, time_hotreload_txtbuf, 30);
		lstrcatW(hotreload_txtbuf, time_hotreload_txtbuf);
		SetWindowTextW(hwnd, hotreload_txtbuf);

		return true;
	}
	return false;
}

void get_dll_path()
{ 
	DWORD l_win32exe = GetModuleFileNameW(NULL, win32_exe_location, MAX_PATH);

	PathCchRemoveFileSpec(win32_exe_location, l_win32exe);

	lstrcpyW(gamecodedll_path, win32_exe_location);
	lstrcatW(gamecodedll_path, gamecodedll_name);

	lstrcpyW(tempgamecodedll_path, win32_exe_location);
	lstrcatW(tempgamecodedll_path, temp_gamecodedll_name);

	SetCurrentDirectoryW(win32_exe_location);
}

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (game_is_ready) { gamecode.wndproc(hWnd, msg, wParam, lParam); }

	switch (msg)
	{
		case WM_SIZE:
			if (game_is_ready && wParam != SIZE_MINIMIZED)
			{
				gamecode.resize(hWnd, LOWORD(lParam), HIWORD(lParam));
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
