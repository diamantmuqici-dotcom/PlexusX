/* Minimal host-side stand-in for <windows.h>: just enough for engine.c + common.h */
#ifndef HOST_SHIM_WINDOWS_H
#define HOST_SHIM_WINDOWS_H
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <wchar.h>
#include <string.h>
#include <stdio.h>

#define WINAPI
#define CALLBACK
#define MAX_PATH 260
#define TRUE 1
#define FALSE 0
typedef int BOOL; typedef unsigned short WORD; typedef uint32_t DWORD; typedef uint32_t UINT; typedef int32_t LONG;
typedef uint64_t ULONGLONG; typedef uint64_t ULONG_PTR; typedef int64_t LONG_PTR; typedef int64_t LPARAM; typedef uint64_t WPARAM; typedef LONG_PTR LRESULT;
typedef void *HANDLE, *HDC, *HMODULE, *HWND, *HINSTANCE, *HICON, *HFONT, *HBRUSH, *HMONITOR, *LPVOID, *HGDIOBJ, *HPEN, *HBITMAP, *HRGN;
typedef const void *LPCVOID; typedef DWORD COLORREF; typedef unsigned char BYTE;
#define _WINDEF_
typedef struct { LONG left, top, right, bottom; } RECT;
typedef struct { LONG x, y; } POINT;
typedef void (*FARPROC)(void);
#define WM_APP 0x8000
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#define GENERIC_READ 0x80000000u
#define GENERIC_WRITE 0x40000000u
#define FILE_SHARE_READ 1
#define OPEN_EXISTING 3
#define CREATE_ALWAYS 2
#define FILE_ATTRIBUTE_NORMAL 0x80
#define MOVEFILE_REPLACE_EXISTING 1
#define MOVEFILE_WRITE_THROUGH 8
#define CP_ACP 0
#define DISPLAY_DEVICE_ATTACHED_TO_DESKTOP 1
typedef struct { DWORD cb; wchar_t DeviceName[32]; wchar_t DeviceString[128]; DWORD StateFlags; wchar_t DeviceID[128]; wchar_t DeviceKey[128]; } DISPLAY_DEVICEW;

/* --- the subset of the API the color engine + pipeline use (implemented in host_mock.c) --- */
DWORD GetTickCount(void);
HMODULE LoadLibraryW(const wchar_t *); FARPROC GetProcAddress(HMODULE, const char *);
HDC CreateDCA(const char *, const char *, const char *, const void *); BOOL DeleteDC(HDC);
BOOL GetDeviceGammaRamp(HDC, LPVOID); BOOL SetDeviceGammaRamp(HDC, LPVOID);
int wsprintfA(char *, const char *, ...); int wsprintfW(wchar_t *, const wchar_t *, ...);
wchar_t *lstrcpyW(wchar_t *, const wchar_t *); wchar_t *lstrcpynW(wchar_t *, const wchar_t *, int);
char *lstrcpynA(char *, const char *, int); int lstrcmpA(const char *, const char *);
int MultiByteToWideChar(UINT, DWORD, const char *, int, wchar_t *, int);
BOOL EnumDisplayDevicesW(const wchar_t *, DWORD, DISPLAY_DEVICEW *, DWORD);
HANDLE CreateFileW(const wchar_t *, DWORD, DWORD, void *, DWORD, DWORD, HANDLE);
BOOL ReadFile(HANDLE, LPVOID, DWORD, DWORD *, void *); BOOL WriteFile(HANDLE, LPCVOID, DWORD, DWORD *, void *);
BOOL CloseHandle(HANDLE); BOOL DeleteFileW(const wchar_t *); BOOL FlushFileBuffers(HANDLE);
BOOL MoveFileExW(const wchar_t *, const wchar_t *, DWORD);
#endif
