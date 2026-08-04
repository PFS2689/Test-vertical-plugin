/*
 * Vertical Shorts Plugin — Windows installer
 *
 * Purpose: copy the embedded OBS plugin files into the path OBS actually
 * scans for third-party modules on Windows:
 *   %ProgramData%\obs-studio\plugins\obs-shorts-vertical\
 *
 * OBS Studio (including 32.2.1) calls GetProgramDataPath("obs-studio/plugins/%module%")
 * and does NOT load plugins from %APPDATA%\obs-studio\plugins.
 *
 * Deliberately avoids third-party installer frameworks (Inno Setup, NSIS,
 * etc.) whose stubs are frequently abused by malware and therefore trip
 * Windows Defender ML heuristics even when the payload is clean.
 *
 * This program:
 *  - does not download anything
 *  - requires Administrator rights (ProgramData is machine-wide)
 *  - does not launch other programs
 *  - does not modify Run keys, services, or browsers
 *  - only writes OBS plugin files under ProgramData\obs-studio\plugins
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
#include "setup_version.h"

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
		if (tmp[i] == L'/')
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
		ShowError(owner, L"Could not create the OBS plugins folder.\r\n\r\n"
				 L"ProgramData installs require Administrator rights.\r\n"
				 L"Re-run Setup and approve the UAC prompt.");
		return FALSE;
	}

	HANDLE file = CreateFileW(destPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
				  FILE_ATTRIBUTE_NORMAL, NULL);
	if (file == INVALID_HANDLE_VALUE) {
		const DWORD err = GetLastError();
		if (err == ERROR_ACCESS_DENIED) {
			ShowError(owner, L"Access denied writing plugin files.\r\n\r\n"
					 L"Close OBS Studio, then re-run Setup as Administrator.");
		} else {
			ShowError(owner, L"Could not write plugin files.\r\n\r\n"
					 L"Close OBS Studio and try again.");
		}
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

/* OBS scans ProgramData, not AppData, for third-party plugins on Windows. */
static BOOL GetPluginsRoot(wchar_t *out, size_t outChars)
{
	wchar_t programData[MAX_PATH];
	const HRESULT hr =
		SHGetFolderPathW(NULL, CSIDL_COMMON_APPDATA, NULL, SHGFP_TYPE_CURRENT, programData);
	if (FAILED(hr))
		return FALSE;
	return SUCCEEDED(StringCchPrintfW(out, outChars, L"%s\\obs-studio\\plugins\\obs-shorts-vertical",
					  programData));
}

/* Older builds incorrectly installed under AppData — remove that dead copy. */
static void RemoveLegacyAppDataInstall(void)
{
	wchar_t appdata[MAX_PATH];
	wchar_t legacyRoot[MAX_PATH];
	wchar_t path[MAX_PATH];

	if (FAILED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, SHGFP_TYPE_CURRENT, appdata)))
		return;
	if (FAILED(StringCchPrintfW(legacyRoot, MAX_PATH, L"%s\\obs-studio\\plugins\\obs-shorts-vertical",
				    appdata)))
		return;

	StringCchPrintfW(path, MAX_PATH, L"%s\\bin\\64bit\\obs-shorts-vertical.dll", legacyRoot);
	DeleteFileW(path);
	StringCchPrintfW(path, MAX_PATH, L"%s\\data\\locale\\en-US.ini", legacyRoot);
	DeleteFileW(path);
	StringCchPrintfW(path, MAX_PATH, L"%s\\INSTALL.txt", legacyRoot);
	DeleteFileW(path);

	StringCchPrintfW(path, MAX_PATH, L"%s\\bin\\64bit", legacyRoot);
	RemoveDirectoryW(path);
	StringCchPrintfW(path, MAX_PATH, L"%s\\bin", legacyRoot);
	RemoveDirectoryW(path);
	StringCchPrintfW(path, MAX_PATH, L"%s\\data\\locale", legacyRoot);
	RemoveDirectoryW(path);
	StringCchPrintfW(path, MAX_PATH, L"%s\\data", legacyRoot);
	RemoveDirectoryW(path);
	RemoveDirectoryW(legacyRoot);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE prev, PWSTR cmdLine, int showCmd)
{
	(void)instance;
	(void)prev;
	(void)cmdLine;
	(void)showCmd;

	wchar_t pluginsRoot[MAX_PATH];
	if (!GetPluginsRoot(pluginsRoot, MAX_PATH)) {
		ShowError(NULL, L"Could not locate the ProgramData folder.");
		return 1;
	}

	wchar_t message[1280];
	StringCchPrintfW(message, 1280,
			 L"Install Vertical Shorts Plugin %S for OBS Studio?\r\n\r\n"
			 L"This open-source installer only copies plugin files to:\r\n"
			 L"%s\r\n\r\n"
			 L"Administrator rights are required (OBS loads plugins from ProgramData).\r\n"
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

	RemoveLegacyAppDataInstall();

	StringCchPrintfW(message, 1280,
			 L"Vertical Shorts Plugin %S was installed.\r\n\r\n"
			 L"Next steps:\r\n"
			 L"1. Start OBS Studio 32.2.1 (or newer)\r\n"
			 L"2. If prompted, enable the plugin in Tools → Plugin Manager, then restart OBS\r\n"
			 L"3. Open View → Docks → Vertical Shorts\r\n"
			 L"   (also available under Tools → Vertical Shorts)\r\n\r\n"
			 L"To uninstall later, delete:\r\n%s",
			 VSP_SETUP_VERSION_A, pluginsRoot);
	MessageBoxW(NULL, message, L"Vertical Shorts Plugin Setup", MB_OK | MB_ICONINFORMATION);
	return 0;
}
