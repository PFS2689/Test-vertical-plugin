/*
 * Vertical Shorts Plugin — minimal per-user Windows installer
 *
 * Purpose: copy the embedded OBS plugin files into
 *   %APPDATA%\obs-studio\plugins\obs-shorts-vertical\
 *
 * Deliberately avoids third-party installer frameworks (Inno Setup, NSIS,
 * etc.) whose stubs are frequently abused by malware and therefore trip
 * Windows Defender ML heuristics even when the payload is clean.
 *
 * This program:
 *  - does not download anything
 *  - does not require Administrator rights
 *  - does not launch other programs
 *  - does not modify Run keys, services, or browsers
 *  - only writes into the current user's OBS plugins folder
 */

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <shlobj.h>
#include <strsafe.h>

#include "setup_resources.h"

#ifndef VSP_SETUP_VERSION_A
#define VSP_SETUP_VERSION_A "0.0.0"
#endif

static void ShowError(HWND owner, const wchar_t *text)
{
	MessageBoxW(owner, text, L"Vertical Shorts Plugin Setup", MB_OK | MB_ICONERROR);
}

static BOOL EnsureDirectoryTree(const wchar_t *path)
{
	wchar_t tmp[MAX_PATH];
	size_t len;
	HRESULT hr = StringCchCopyW(tmp, MAX_PATH, path);
	if (FAILED(hr))
		return FALSE;

	len = wcslen(tmp);
	for (size_t i = 0; i < len; ++i) {
		if (tmp[i] == L'/' )
			tmp[i] = L'\\';
	}

	for (size_t i = 3; i < len; ++i) {
		if (tmp[i] != L'\\')
			continue;
		tmp[i] = L'\0';
		if (GetFileAttributesW(tmp) == INVALID_FILE_ATTRIBUTES) {
			if (!CreateDirectoryW(tmp, NULL)) {
				const DWORD err = GetLastError();
				if (err != ERROR_ALREADY_EXISTS)
					return FALSE;
			}
		}
		tmp[i] = L'\\';
	}

	if (GetFileAttributesW(tmp) == INVALID_FILE_ATTRIBUTES) {
		if (!CreateDirectoryW(tmp, NULL)) {
			const DWORD err = GetLastError();
			if (err != ERROR_ALREADY_EXISTS)
				return FALSE;
		}
	}
	return TRUE;
}

static BOOL WriteResourceToFile(HWND owner, int resourceId, const wchar_t *destPath)
{
	HRSRC hrsrc = FindResourceW(NULL, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
	if (!hrsrc) {
		ShowError(owner, L"Missing embedded plugin payload.");
		return FALSE;
	}

	HGLOBAL hglobal = LoadResource(NULL, hrsrc);
	if (!hglobal) {
		ShowError(owner, L"Could not load embedded plugin payload.");
		return FALSE;
	}

	const DWORD size = SizeofResource(NULL, hrsrc);
	const void *data = LockResource(hglobal);
	if (!data || size == 0) {
		ShowError(owner, L"Embedded plugin payload is empty.");
		return FALSE;
	}

	wchar_t dir[MAX_PATH];
	HRESULT hr = StringCchCopyW(dir, MAX_PATH, destPath);
	if (FAILED(hr))
		return FALSE;

	wchar_t *slash = wcsrchr(dir, L'\\');
	if (!slash) {
		ShowError(owner, L"Invalid destination path.");
		return FALSE;
	}
	*slash = L'\0';
	if (!EnsureDirectoryTree(dir)) {
		ShowError(owner, L"Could not create the OBS plugins folder.");
		return FALSE;
	}

	HANDLE file = CreateFileW(destPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
				  FILE_ATTRIBUTE_NORMAL, NULL);
	if (file == INVALID_HANDLE_VALUE) {
		ShowError(owner, L"Could not write plugin files.\n\n"
				 L"Close OBS Studio and try again.");
		return FALSE;
	}

	DWORD written = 0;
	const BOOL ok = WriteFile(file, data, size, &written, NULL) && written == size;
	CloseHandle(file);
	if (!ok) {
		DeleteFileW(destPath);
		ShowError(owner, L"Failed while writing plugin files.");
		return FALSE;
	}
	return TRUE;
}

static BOOL GetPluginsRoot(wchar_t *out, size_t outChars)
{
	wchar_t appdata[MAX_PATH];
	const HRESULT hr = SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, SHGFP_TYPE_CURRENT, appdata);
	if (FAILED(hr))
		return FALSE;
	return SUCCEEDED(StringCchPrintfW(out, outChars, L"%s\\obs-studio\\plugins\\obs-shorts-vertical",
					  appdata));
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE prev, PWSTR cmdLine, int showCmd)
{
	(void)instance;
	(void)prev;
	(void)cmdLine;
	(void)showCmd;

	wchar_t pluginsRoot[MAX_PATH];
	if (!GetPluginsRoot(pluginsRoot, MAX_PATH)) {
		ShowError(NULL, L"Could not locate your AppData folder.");
		return 1;
	}

	wchar_t message[1024];
	StringCchPrintfW(message, 1024,
			 L"Install Vertical Shorts Plugin %S for OBS Studio?\r\n\r\n"
			 L"This open-source installer only copies plugin files to:\r\n"
			 L"%s\r\n\r\n"
			 L"No administrator rights are required.\r\n"
			 L"Please close OBS Studio before continuing.",
			 VSP_SETUP_VERSION_A, pluginsRoot);

	const int answer = MessageBoxW(NULL, message, L"Vertical Shorts Plugin Setup",
				       MB_OKCANCEL | MB_ICONINFORMATION | MB_DEFBUTTON1);
	if (answer != IDOK)
		return 0;

	wchar_t dllPath[MAX_PATH];
	wchar_t localePath[MAX_PATH];
	wchar_t installTxtPath[MAX_PATH];
	StringCchPrintfW(dllPath, MAX_PATH, L"%s\\bin\\64bit\\obs-shorts-vertical.dll", pluginsRoot);
	StringCchPrintfW(localePath, MAX_PATH, L"%s\\data\\locale\\en-US.ini", pluginsRoot);
	StringCchPrintfW(installTxtPath, MAX_PATH, L"%s\\INSTALL.txt", pluginsRoot);

	if (!WriteResourceToFile(NULL, IDR_PLUGIN_DLL, dllPath))
		return 1;
	if (!WriteResourceToFile(NULL, IDR_LOCALE_INI, localePath))
		return 1;
	if (!WriteResourceToFile(NULL, IDR_INSTALL_TXT, installTxtPath))
		return 1;

	StringCchPrintfW(message, 1024,
			 L"Vertical Shorts Plugin %S was installed.\r\n\r\n"
			 L"Next steps:\r\n"
			 L"1. Start OBS Studio\r\n"
			 L"2. Open View → Docks → Vertical Shorts\r\n\r\n"
			 L"To uninstall later, delete:\r\n%s",
			 VSP_SETUP_VERSION_A, pluginsRoot);
	MessageBoxW(NULL, message, L"Vertical Shorts Plugin Setup", MB_OK | MB_ICONINFORMATION);
	return 0;
}
