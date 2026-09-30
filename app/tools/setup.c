/* PlexusX — Lightweight Standalone Windows Installer
 * Installs to %LocalAppData%\Programs\PlexusX\ (No Admin Required)
 * Creates Desktop & Start Menu Shortcuts, registers Uninstall, launches PlexusX.
 */
#define WIN32_LEAN_AND_MEAN
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <objbase.h>
#include <shlwapi.h>
#include <stdio.h>

extern const unsigned char g_payload_exe[];
extern const unsigned int  g_payload_exe_len;

static void create_shortcut(const wchar_t *target_path, const wchar_t *shortcut_path, const wchar_t *desc)
{
    IShellLinkW *psl = NULL;
    HRESULT hr = CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkW, (void **)&psl);
    if (SUCCEEDED(hr)) {
        psl->lpVtbl->SetPath(psl, target_path);
        psl->lpVtbl->SetDescription(psl, desc);
        wchar_t work_dir[MAX_PATH];
        lstrcpynW(work_dir, target_path, MAX_PATH);
        PathRemoveFileSpecW(work_dir);
        psl->lpVtbl->SetWorkingDirectory(psl, work_dir);

        IPersistFile *ppf = NULL;
        hr = psl->lpVtbl->QueryInterface(psl, &IID_IPersistFile, (void **)&ppf);
        if (SUCCEEDED(hr)) {
            ppf->lpVtbl->Save(ppf, shortcut_path, TRUE);
            ppf->lpVtbl->Release(ppf);
        }
        psl->lpVtbl->Release(psl);
    }
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, LPWSTR cmd, int show)
{
    (void)inst; (void)prev; (void)cmd; (void)show;
    CoInitialize(NULL);

    int silent = (cmd && wcsstr(cmd, L"/S") != NULL);

    if (!silent) {
        int r = MessageBoxW(NULL,
            L"Welcome to PlexusX Setup!\n\n"
            L"PlexusX will be installed to your local user profile:\n"
            L"%LocalAppData%\\Programs\\PlexusX\n\n"
            L"Do you want to continue?",
            L"PlexusX Setup", MB_YESNO | MB_ICONQUESTION);
        if (r != IDYES) return 0;
    }

    wchar_t local_app_data[MAX_PATH];
    if (SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, local_app_data) != S_OK) {
        MessageBoxW(NULL, L"Failed to locate %LocalAppData%", L"PlexusX Setup Error", MB_ICONERROR);
        return 1;
    }

    wchar_t install_dir[MAX_PATH];
    wsprintfW(install_dir, L"%s\\Programs\\PlexusX", local_app_data);
    SHCreateDirectoryExW(NULL, install_dir, NULL);

    wchar_t dest_exe[MAX_PATH];
    wsprintfW(dest_exe, L"%s\\PlexusX.exe", install_dir);

    /* Write embedded binary */
    HANDLE hFile = CreateFileW(dest_exe, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        MessageBoxW(NULL, L"Could not write destination executable. Please close any running instances of PlexusX and retry.",
                    L"PlexusX Setup Error", MB_ICONERROR);
        return 1;
    }

    DWORD written = 0;
    WriteFile(hFile, g_payload_exe, g_payload_exe_len, &written, NULL);
    CloseHandle(hFile);

    /* Desktop Shortcut */
    wchar_t desktop[MAX_PATH];
    if (SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, desktop) == S_OK) {
        wchar_t sc_path[MAX_PATH];
        wsprintfW(sc_path, L"%s\\PlexusX.lnk", desktop);
        create_shortcut(dest_exe, sc_path, L"PlexusX Gaming Display Optimizer");
    }

    /* Start Menu Shortcut */
    wchar_t start_menu[MAX_PATH];
    if (SHGetFolderPathW(NULL, CSIDL_PROGRAMS, NULL, 0, start_menu) == S_OK) {
        wchar_t sm_dir[MAX_PATH];
        wsprintfW(sm_dir, L"%s\\PlexusX", start_menu);
        CreateDirectoryW(sm_dir, NULL);
        wchar_t sc_path[MAX_PATH];
        wsprintfW(sc_path, L"%s\\PlexusX.lnk", sm_dir);
        create_shortcut(dest_exe, sc_path, L"PlexusX Gaming Display Optimizer");
    }

    /* Register in HKCU Uninstall */
    HKEY hKey;
    if (RegCreateKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\PlexusX",
        0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {

        wchar_t str_disp[] = L"PlexusX (Gaming Display Optimizer)";
        RegSetValueExW(hKey, L"DisplayName", 0, REG_SZ, (BYTE *)str_disp, sizeof(str_disp));

        wchar_t str_ver[] = L"2.0.0";
        RegSetValueExW(hKey, L"DisplayVersion", 0, REG_SZ, (BYTE *)str_ver, sizeof(str_ver));

        wchar_t str_pub[] = L"PlexusX Open Source";
        RegSetValueExW(hKey, L"Publisher", 0, REG_SZ, (BYTE *)str_pub, sizeof(str_pub));

        RegSetValueExW(hKey, L"DisplayIcon", 0, REG_SZ, (BYTE *)dest_exe, (DWORD)(lstrlenW(dest_exe) + 1) * sizeof(wchar_t));

        wchar_t uninst_cmd[MAX_PATH + 32];
        wsprintfW(uninst_cmd, L"cmd.exe /c rd /s /q \"%s\"", install_dir);
        RegSetValueExW(hKey, L"UninstallString", 0, REG_SZ, (BYTE *)uninst_cmd, (DWORD)(lstrlenW(uninst_cmd) + 1) * sizeof(wchar_t));

        RegCloseKey(hKey);
    }

    if (!silent) {
        int r = MessageBoxW(NULL,
            L"PlexusX has been successfully installed!\n\n"
            L"Would you like to launch PlexusX now?",
            L"PlexusX Setup Complete", MB_YESNO | MB_ICONINFORMATION);
        if (r == IDYES) {
            ShellExecuteW(NULL, L"open", dest_exe, NULL, install_dir, SW_SHOWNORMAL);
        }
    } else {
        ShellExecuteW(NULL, L"open", dest_exe, NULL, install_dir, SW_SHOWNORMAL);
    }

    CoUninitialize();
    return 0;
}
