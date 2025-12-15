/******************************************************************************

Recaps - change language and keyboard layout using the CapsLock key.
Copyright (C) 2007 Eli Golovinsky
Copyright (C) 2017-2024 Siarzhuk Zharski

-------------------------------------------------------------------------------

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

*******************************************************************************/

#include "stdafx.h"
#include "recaps.h"
#include "resource.h"
#include "configuration.h"
#include "trayicon.h"
#include "fixlayouts.h"
#include "settings.h"
#include "utils.h"


#if _MSC_VER < 1500  // older than VS2008

// Hook stuff
#define WH_KEYBOARD_LL 13
#define LLKHF_INJECTED 0x10
typedef struct {
	DWORD vkCode;
	DWORD scanCode;
	DWORD flags;
	DWORD time;
	ULONG_PTR dwExtraInfo;
} KBDLLHOOKSTRUCT, *PKBDLLHOOKSTRUCT;

#endif // _MSC_VER < 1500

// Tray icon constants
#define APPWM_TRAYICON       WM_APP

// General constants
#define MUTEX L"recaps-D3E743A3-E0F9-47f5-956A-CD15C6548789"
#define WINDOWCLASS_NAME L"RECAPS"
#define TITLE L"Recaps"


HINSTANCE			g_hInstance = NULL;
HICON					g_hIcon = NULL;
HWND					g_hwndMessageWindow = NULL;

std::vector<HWND> g_hwndOverlayWindows;

static BOOL		st_bModalShown = FALSE;
static HHOOK	st_hHook = NULL;
static HKL		st_hklCurrent = NULL;
//static HWND		st_hwndFocusCurrent = NULL;
static HICON	st_hCurIcon = NULL;
//static ATOM		st_atomButtonClass = NULL;
static int		st_nOpacity = 0;

static HANDLE st_threadInject = INVALID_HANDLE_VALUE;
static UHK    st_uhkInject(0);
static HANDLE st_hInjectEvent = NULL;
static volatile bool st_bInjectThreadStop = false;

static HANDLE st_hRegChangeEvent = 0;
static HKEY   st_hLocalesListKey = 0;

static LRESULT CALLBACK WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
static LRESULT CALLBACK LowLevelHookProc(int nCode, WPARAM wParam, LPARAM lParam);

static void CALLBACK TimerProc(HWND hwnd, UINT uiMessage, UINT_PTR uiTimerId, DWORD dwMilliseconds);
static void CALLBACK FadeOffProc(HWND hwnd, UINT uiMessage, UINT_PTR uiTimerId, DWORD dwMilliseconds);

static int OnTrayIcon(HWND hWnd, WPARAM wParam, LPARAM lParam);
static int OnCommand(HWND hWnd, WORD wID, HWND hCtl);
static BOOL ShowPopupMenu(HWND hWnd);
static void GetKeyboardLayouts(std::map<HKL, KeyboardLayoutInfo>& info);
static void SetScrollLockLED(bool bOn);
static void SwitchAndConvertThread(void*);
static unsigned __stdcall InjectInputThread(void*);
static HKL GetCurrentLayout(HWND* lphwndFocus = NULL);
static HWND RemoteGetFocus();
static void ProcessHotkey(UHK uhkHotkey);
static bool SetupRegChangeNotify(bool bReset);
static bool OnRegChangeNotify();


static HWND CreateOverlayHintWindow(LPRECT lpRect, HINSTANCE hInstance)
{
	int nWidth = 32 + 2;
	int nHeight = 32 + 2;
	int nX0 = ((lpRect->right + lpRect->left) - nWidth) / 2;
	int nY0 = ((lpRect->bottom + lpRect->top) - nHeight) / 2;

	HWND hwndMessageWindow = CreateWindowEx(
		WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_LAYERED,
		WINDOWCLASS_NAME,	TITLE, WS_POPUP,
		nX0, nY0, nWidth, nHeight, HWND_DESKTOP, 0, hInstance, 0);

	SetLayeredWindowAttributes(hwndMessageWindow, RGB(0, 0, 0), 128, LWA_ALPHA);
	return hwndMessageWindow;
}


static BOOL CALLBACK MonitorEnumProc(HMONITOR, HDC, LPRECT lpRect, LPARAM)
{
	if (NULL != lpRect) {
		HWND hwnd = CreateOverlayHintWindow(lpRect, g_hInstance);
		if (NULL != hwnd)
			g_hwndOverlayWindows.push_back(hwnd);
	}

	return TRUE;
}


///////////////////////////////////////////////////////////////////////////////
// Program's entry point
int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
		LPSTR lpCmdLine, int nCmdShow)
{
	UNREFERENCED_PARAMETER(hInstance);
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);
	UNREFERENCED_PARAMETER(nCmdShow);

	// Prevent from two copies of Recaps from running at the same time
	HANDLE mutex = CreateMutex(NULL, FALSE, MUTEX);
	DWORD result = WaitForSingleObject(mutex, 0);
	if (result == WAIT_TIMEOUT) {
		MessageBox(NULL, L"Recaps is already running.", L"Recaps",
				MB_OK | MB_ICONINFORMATION);
		return 1;
	}

	// Create a fake window to listen to events
	WNDCLASSEX wclx = {0};
	wclx.cbSize         = sizeof( wclx );
	wclx.lpfnWndProc    = &WindowProc;
	wclx.hInstance      = hInstance;
	wclx.lpszClassName  = WINDOWCLASS_NAME;
	RegisterClassEx(&wclx);

	g_hInstance = hInstance;
	InitializeCriticalSection(&g_csLayouts);

	if (EnumDisplayMonitors(NULL, NULL, MonitorEnumProc, 0)
			&& g_hwndOverlayWindows.size() > 0) {
		g_hwndMessageWindow = g_hwndOverlayWindows[0];

	} else {
		RECT rect;
		SetRect(&rect, 0, 0,
			GetSystemMetrics(SM_CXFULLSCREEN),
			GetSystemMetrics(SM_CYFULLSCREEN));
		g_hwndMessageWindow = CreateOverlayHintWindow(&rect, hInstance);
	}

	// Set hook to capture CapsLock
	st_hHook = SetWindowsHookEx(WH_KEYBOARD_LL,
			LowLevelHookProc, GetModuleHandle(NULL), 0);

	g_hIcon = (HICON)LoadImage(hInstance,
			MAKEINTRESOURCE(IDI_MAINFRAME), IMAGE_ICON, 16, 16, 0);
	g_hHKIcon = (HICON)LoadImage(hInstance,
			MAKEINTRESOURCE(IDI_HOTKEY), IMAGE_ICON, 16, 16, 0);

	// Initialize
/*	WNDCLASSEX wcx = { 0 };
	wcx.cbSize = sizeof(WNDCLASSEXA);
	st_atomButtonClass = (ATOM)GetClassInfoEx(NULL, _T("BUTTON"), &wcx); */

	st_hInjectEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	st_threadInject = (HANDLE)_beginthreadex(NULL, 0, InjectInputThread, NULL, 0, NULL);

	GetKeyboardLayouts(g_keyboardInfo);
	LoadConfiguration(g_keyboardInfo);
	AddTrayIcon(g_hwndMessageWindow, 0, APPWM_TRAYICON, g_hIcon, TITLE);

	SetupRegChangeNotify(false);
	
	SetTimer(g_hwndMessageWindow, 1, 500, TimerProc);

	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);

	// Handle messages
	MSG msg;
	while (GetMessage(&msg, NULL, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	// Clean up
	st_bInjectThreadStop = true;
	if (NULL != st_hInjectEvent)
		SetEvent(st_hInjectEvent);
	if (INVALID_HANDLE_VALUE != st_threadInject) {
		WaitForSingleObject(st_threadInject, 1000);
		CloseHandle(st_threadInject);
	}
	if (NULL != st_hInjectEvent)
		CloseHandle(st_hInjectEvent);

	RemoveTrayIcon(g_hwndMessageWindow, 0);
	UnregisterClass(WINDOWCLASS_NAME, hInstance);

	for (int i = 0; i < g_hwndOverlayWindows.size(); i++)
		DestroyWindow(g_hwndOverlayWindows[i]);
	g_hwndOverlayWindows.clear(),

	DestroyWindow(g_hwndMessageWindow);
	g_hwndMessageWindow = NULL;

	SaveConfiguration(g_keyboardInfo);
	UnhookWindowsHookEx(st_hHook);

	CloseHandle(st_hRegChangeEvent);
	RegCloseKey(st_hLocalesListKey);
	DeleteCriticalSection(&g_csLayouts);

	return 0;
}


///////////////////////////////////////////////////////////////////////////////
// Handles events at the window (both hot key and from the tray icon)
LRESULT CALLBACK WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg) {
		case WM_PAINT: {
			PAINTSTRUCT ps;
			HDC hdc = BeginPaint(hWnd, &ps);

			if (NULL != st_hCurIcon) {
				// All painting occurs here, between BeginPaint and EndPaint.
				FillRect(hdc, &ps.rcPaint, (HBRUSH) (COLOR_GRAYTEXT));
				int w = ps.rcPaint.right - ps.rcPaint.left;
				int h = ps.rcPaint.bottom -ps.rcPaint.top;
				DrawIconEx(hdc, 1, 1, st_hCurIcon, w - 2, h - 2, 0, NULL, DI_NORMAL);
			}

			EndPaint(hWnd, &ps);
		}
		return 0;

		case APPWM_TRAYICON:
			return OnTrayIcon(hWnd, wParam, lParam);

		case WM_COMMAND:
			return OnCommand(hWnd, LOWORD(wParam), (HWND)lParam);

		case WM_CLOSE:
			PostQuitMessage(0);
			return 0;

		default:
/*#ifdef _DEBUG
	TCHAR ch[1024] = { 0 };
	_stprintf(ch, _T("MSG:%08x WP:%08x LP:%08x\n"), uMsg, wParam, lParam);
	OutputDebugString(ch);
#endif*/
			break;
	}

	return DefWindowProc(hWnd, uMsg, wParam, lParam);
}


void CALLBACK TimerProc(HWND hWnd, UINT, UINT_PTR, DWORD)
{
	HWND hwndFocus = RemoteGetFocus();

	HKL hklCurrent = GetKeyboardLayout(
		GetWindowThreadProcessId(hwndFocus, NULL));

	bool bUpdateOverlayIcon = false;
	if (hklCurrent != st_hklCurrent) {
		HICON hIconLocal = NULL;
		std::basic_string<TCHAR> nameLocal;
		bool useLedLocal = false;
		{
			LayoutsGuard guard;
			for (std::map<HKL, KeyboardLayoutInfo>::iterator i = g_keyboardInfo.begin();
				i != g_keyboardInfo.end(); i++)
				if (i->first == hklCurrent) {
					useLedLocal = !!i->second.useLED;
					hIconLocal = i->second.showIcon ? i->second.iconColor : NULL;
					nameLocal = i->second.name;
					break;
				}
		}
		if (!nameLocal.empty()) {
			SetScrollLockLED(useLedLocal);
			st_hCurIcon = hIconLocal;
			ModifyTrayIcon(hWnd, 0, hIconLocal, nameLocal.c_str());
			//bUpdateOverlayIcon = true; // i->second.showIcon;
			ShowFadeOffIcon(NULL != st_hCurIcon /* ? st_hwndFocusCurrent : NULL*/);
		}

		PrintDebugString("%08X -> %08X", st_hklCurrent, hklCurrent);

		st_hklCurrent = hklCurrent;
	}

/*	if (st_hwndFocusCurrent != hwndFocus) {
		st_hwndFocusCurrent
			= st_atomButtonClass != GetClassLong(hwndFocus, GCW_ATOM)
					? hwndFocus : NULL;
//		bUpdateOverlayIcon = true; // NULL != st_hCurIcon;
	} */

//	if (bUpdateOverlayIcon)
	//	ShowFadeOffIcon(NULL != st_hCurIcon /* ? st_hwndFocusCurrent : NULL*/);

	if (WAIT_OBJECT_0 == WaitForSingleObject(st_hRegChangeEvent, 0)) {
		OnRegChangeNotify();
		SetupRegChangeNotify(true);
	}
}


void CALLBACK FadeOffProc(
		HWND hWnd, UINT uiMessage, UINT_PTR uiTimerId, DWORD dwMilliseconds)
{
	st_nOpacity -= g_btFadeOffSpeed;

	bool bAtFinalOpacity = st_nOpacity < g_btFinishOpacity;
	if (bAtFinalOpacity) {
		KillTimer(hWnd, uiTimerId);
		st_nOpacity = 0;
	}

	for (int i = 0; i < g_hwndOverlayWindows.size(); i++)
		SetLayeredWindowAttributes(g_hwndOverlayWindows[i], 0,
			bAtFinalOpacity ? g_btFinishOpacity : st_nOpacity, LWA_ALPHA);

}


///////////////////////////////////////////////////////////////////////////////
// Create and display a popup menu when the user right-clicks on the icon
int OnTrayIcon(HWND hWnd, WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(wParam);

	if (st_bModalShown == TRUE)
		return 0;

	switch (lParam) {
		case WM_RBUTTONUP:
			// Show the context menu
			ShowPopupMenu(hWnd);
			return 0;
		case WM_LBUTTONDBLCLK:
			PostMessage(hWnd, WM_COMMAND, MAKEWPARAM(IDM_LANG_SETTINGS, 0), 0);
			return 0;
	}
	return 0;
}


static bool SetupRegChangeNotify(bool bReset)
{
	if (!bReset) {
		if (ERROR_SUCCESS != RegOpenKeyEx(HKEY_CURRENT_USER,
			 _T("Keyboard Layout\\Preload"), 0, KEY_NOTIFY, &st_hLocalesListKey))
			return false;
		st_hRegChangeEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
		if (NULL == st_hRegChangeEvent)
			return false;
	}

	return ERROR_SUCCESS != RegNotifyChangeKeyValue(
			st_hLocalesListKey, TRUE, REG_NOTIFY_CHANGE_LAST_SET, st_hRegChangeEvent, TRUE);
}


///////////////////////////////////////////////////////////////////////////////
// Handles user commands from the menu
int OnCommand(HWND hWnd, WORD wID, HWND hCtl)
{
	UNREFERENCED_PARAMETER(hCtl);

	//  Have a look at the command and act accordingly
	switch (wID) {
	case IDM_EXIT:
		PostMessage( hWnd, WM_CLOSE, 0, 0 );
		break;

	case IDM_ABOUT:
		AboutBoxDialog(NULL);
		break;

	case IDM_STARTUP:
		RegCheckAndSetRunAtLogon(true);
		break;

	case IDM_LANG_CONFIG:
		WinExec("control.exe input.dll", SW_NORMAL);
		break;

	case IDM_LANG_SETTINGS:
		RunConfiguration(NULL);
		SaveConfiguration(g_keyboardInfo);
		break;
	}

	return 0;
}


///////////////////////////////////////////////////////////////////////////////
// Create and display a popup menu when the user right-clicks on the icon
BOOL ShowPopupMenu(HWND hWnd)
{
	HMENU   hPop        = NULL;
	BOOL    cmd;
	POINT   curpos;

	// Create the menu
	int idx = 0;
	hPop = CreatePopupMenu();
	InsertMenu(hPop, idx++, MF_STRING | MF_HILITE, IDM_ABOUT, L"About...");
	InsertMenu(hPop, idx++, MF_SEPARATOR, 0, NULL);

	// run at log on setting
	UINT flags = MF_BYPOSITION | MF_STRING;
	if (RegCheckAndSetRunAtLogon(false))
		flags |= MF_CHECKED;

	InsertMenu(hPop, idx++, flags, IDM_STARTUP, _T("Run at user log on"));

	InsertMenu(hPop, idx++, MF_BYPOSITION | MF_STRING, IDM_LANG_SETTINGS, _T("Recaps Settings ..."));
	InsertMenu(hPop, idx++, MF_BYPOSITION | MF_STRING, IDM_LANG_CONFIG, _T("System Configuration ..."));
	InsertMenu(hPop, idx++, MF_SEPARATOR, 0, NULL);
	InsertMenu(hPop, idx++, MF_BYPOSITION | MF_STRING, IDM_EXIT, L"Exit");

	SetMenuDefaultItem(hPop, IDM_LANG_SETTINGS, FALSE);

	// Show the menu

	// See http://support.microsoft.com/kb/135788 for the reasons
	// for the SetForegroundWindow and Post Message trick.
	GetCursorPos(&curpos);
	SetForegroundWindow(hWnd);
	st_bModalShown = TRUE;
	cmd = (WORD)TrackPopupMenu(
			hPop, TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
			curpos.x, curpos.y, 0, hWnd, NULL);
	PostMessage(hWnd, WM_NULL, 0, 0);
	st_bModalShown = FALSE;

	// Send a command message to the window to handle the menu item the user chose
	if (cmd)
		SendMessage(hWnd, WM_COMMAND, cmd, 0);

	DestroyMenu(hPop);

	return cmd;
}


void SetScrollLockLED(bool bOn)
{
	if (g_bCustomizingOn)
		return;

/*#ifdef _DEBUG
	TCHAR ch[1024] = { 0 };
	_stprintf(ch, _T("SetScrollLockLED:%d != %d = %d\n"), bOn, GetKeyState(VK_SCROLL),
		bOn != (0 != GetKeyState(VK_SCROLL)));
	OutputDebugString(ch);
#endif*/
	if (bOn != (0 != GetKeyState(VK_SCROLL))) {
#if 0
		INPUT input[2] = { 0 };
		input[0].type = input[1].type = INPUT_KEYBOARD;
		input[0].ki.wVk  = input[1].ki.wVk = VK_SCROLL;
		input[1].ki.dwFlags = KEYEVENTF_KEYUP;
		UINT u = ::SendInput(2, input, sizeof(INPUT));
/*#ifdef _DEBUG
		_stprintf(ch, _T("SendInput:%d %d\n"), u, GetLastError());
		OutputDebugString(ch);
#endif*/
#else
		SendKeyCombo(0, VK_SCROLL, FALSE);
#endif

	}
}


static bool GetKeyboardLayoutName(HKL hkl, std::basic_string<TCHAR>& name)
{
	bool bResult = false;
	TCHAR szKLID[KL_NAMELENGTH] = { 0 };
	if (0 != ActivateKeyboardLayout(hkl, 0)
			&& 0 != GetKeyboardLayoutName(szKLID)) {

		std::basic_string<TCHAR>
			path(_T("SYSTEM\\CurrentControlSet\\Control\\Keyboard Layouts\\"));
		path += szKLID;

		DWORD dwSize = 128;
		name.resize(dwSize);
		if (bResult = (ERROR_SUCCESS == RegGetValue(HKEY_LOCAL_MACHINE,
				path.c_str(), _T("Layout Text"), RRF_RT_REG_SZ, NULL, &name[0], &dwSize))) {
			name.resize(_tcslen(name.c_str()));
		} else
			name.clear();
	}

	return bResult;
}


///////////////////////////////////////////////////////////////////////////////
// Fills ``info`` with the currently installed keyboard layouts
// Based on http://blogs.msdn.com/michkap/archive/2004/12/05/275231.aspx.

void GetKeyboardLayouts(std::map<HKL, KeyboardLayoutInfo>& info)
{
	int count = GetKeyboardLayoutList(0, NULL);
	std::vector<HKL> hkls(count);
	GetKeyboardLayoutList(count, &hkls[0]);

	LayoutsGuard guard;
	for (UINT i = 0; i < count; i++) {
		UINT_PTR hklValue = reinterpret_cast<UINT_PTR>(hkls[i]);
		LANGID language = static_cast<LANGID>(hklValue & 0x0000FFFF); // bottom 16 bits
		LCID locale = MAKELCID(language, SORT_DEFAULT);
		int len = GetLocaleInfo(locale, LOCALE_SENGLANGUAGE, NULL, 0);
		std::basic_string<TCHAR> abbrName(len, 0);
		GetLocaleInfo(locale, LOCALE_SENGLANGUAGE, &abbrName[0], len);
		abbrName.resize(len - 1);

		std::basic_string<TCHAR> layoutName(len, 0);
		GetKeyboardLayoutName(hkls[i], layoutName);

		len = GetLocaleInfo(locale, LOCALE_SENGLISHDISPLAYNAME, NULL, 0);
		std::basic_string<TCHAR> localeName(len, 0);
		GetLocaleInfo(locale, LOCALE_SENGLISHDISPLAYNAME, &localeName[0], len);
		localeName.resize(len - 1);
		localeName += _T(" [ ");
		localeName += layoutName;
		localeName += _T(" ] ");
		g_keyboardInfo[hkls[i]] = KeyboardLayoutInfo(static_cast<int>(i), localeName.c_str(), abbrName.c_str());
	}
}


bool OnRegChangeNotify()
{
	int count = GetKeyboardLayoutList(0, NULL);
	std::vector<HKL> hkls(count);
	GetKeyboardLayoutList(count, &hkls[0]);

	bool bOk = false;
	{
		LayoutsGuard guard;
		bOk = count == g_keyboardInfo.size();
		for (int i = 0; bOk && i < count; i++)
			bOk = g_keyboardInfo.find(hkls[i]) != g_keyboardInfo.end();

		if (!bOk) {
			g_keyboardInfo.clear();
			g_hotkeyInfo.clear();
			GetKeyboardLayouts(g_keyboardInfo);
			LoadConfiguration(g_keyboardInfo);
		}
	}

	return !bOk;
}


///////////////////////////////////////////////////////////////////////////////
// Finds out which window has the focus
HWND RemoteGetFocus()
{
	HWND hwnd = GetForegroundWindow();
	DWORD remoteThreadId = GetWindowThreadProcessId(hwnd, NULL);
	DWORD currentThreadId = GetCurrentThreadId();
/*	AttachThreadInput(remoteThreadId, currentThreadId, TRUE);
	HWND focused = GetFocus();
	AttachThreadInput(remoteThreadId, currentThreadId, FALSE);
*/
	GUITHREADINFO ti = { 0 };
	ti.cbSize = sizeof(GUITHREADINFO);
	GetGUIThreadInfo(remoteThreadId, &ti);

/*#if _DEBUG
	TCHAR ch[1024] = { 0 };
	_stprintf(ch, _T("RemoteGetFocus:%08x ==? %08x\n"), focused,  ti.hwndFocus);
	OutputDebugString(ch);
#endif*/

	return ti.hwndFocus;
}


///////////////////////////////////////////////////////////////////////////////
// Returns the current layout in the active window
HKL GetCurrentLayout(HWND* lphwndFocus /*= NULL*/)
{
	HWND hwnd = RemoteGetFocus();
	if (NULL != lphwndFocus)
		*lphwndFocus = hwnd;
	DWORD threadId = GetWindowThreadProcessId(hwnd, NULL);
	return GetKeyboardLayout(threadId);
}


///////////////////////////////////////////////////////////////////////////////
// Switches the current language
static void SwitchKeyboardLayout(HWND hwnd, HKL hklSource, HKL hklTarget)
{
	PostMessage(hwnd, WM_INPUTLANGCHANGEREQUEST, 0, (LPARAM)(hklTarget));

	std::map<HKL, KeyboardLayoutInfo>::iterator i_hotkeyl
			= g_keyboardInfo.end();
	HICON hIconColor = NULL;
	std::basic_string<TCHAR> nameLocal;
	bool useLedLocal = false;
	bool hasTarget = false;
	{
		LayoutsGuard guard;
		i_hotkeyl = g_keyboardInfo.find(hklTarget);
		if (i_hotkeyl != g_keyboardInfo.end()) {
			hasTarget = true;
			useLedLocal = !!i_hotkeyl->second.useLED;
			hIconColor = i_hotkeyl->second.iconColor;
			st_hCurIcon = i_hotkeyl->second.showIcon ? i_hotkeyl->second.iconColor : NULL;
			nameLocal = i_hotkeyl->second.name;
		}
	}

	if (hasTarget) {
		SetScrollLockLED(useLedLocal);
		ModifyTrayIcon(g_hwndMessageWindow, 0, hIconColor, nameLocal.c_str());
#ifdef _DEBUG
		PrintDebugString(_T("Language set to %s from %08x"), nameLocal.c_str(), hklSource);
#endif
	} else {
#ifdef _DEBUG
	PrintDebugString(_T("Language set to %08x from %08x"), hklTarget, hklSource);
#endif
	}

	static HANDLE st_uThreadId = 0;

	if (NULL != hklSource) {
		DWORD dwExitCode = -1;
		if (!GetExitCodeThread(st_uThreadId, &dwExitCode)
				|| STILL_ACTIVE != dwExitCode) {
		  static HKL hkls[2] = { hklSource, hklTarget };
		  hkls[0] = hklSource;
		  hkls[1] = hklTarget;
			// We start SwitchLayoutAndConvertSelected in another thread
			// since it simulates keystrokes to copy and paste the teset
			// which call back into this hook. That isn't good..
			st_uThreadId = (HANDLE)_beginthread(SwitchAndConvertThread, 0, hkls);
		} else
			MessageBeep(MB_ICONERROR);
	}
}


static HKL EjectCD()
{
	DWORD dwMask = GetLogicalDrives();
	TCHAR drive[7] = { _T("A:\\") };
	for (int i = 0; i < 32; i++) {
		if (dwMask & (1 << i)) {
			drive[0] = TCHAR('A' + i);
			if (DRIVE_CDROM == GetDriveType(drive)) {
				drive[0] = '\\';
				drive[1] = '\\';
				drive[2] = '.';
				drive[3] = '\\';
				drive[4] = TCHAR('A' + i);
				drive[5] = ':';
				drive[6] = '\0';
				HANDLE handle = CreateFile(drive,
					GENERIC_READ, FILE_SHARE_WRITE, 0, OPEN_EXISTING, 0, 0);
				DWORD bytes = 0;
				DeviceIoControl(handle, FSCTL_LOCK_VOLUME, 0, 0, 0, 0, &bytes, 0);
				DeviceIoControl(handle, FSCTL_DISMOUNT_VOLUME, 0, 0, 0, 0, &bytes, 0);
				DeviceIoControl(handle, IOCTL_STORAGE_EJECT_MEDIA, 0, 0, 0, 0, &bytes, 0);
				CloseHandle(handle);
			}
		}
	}

	return NULL;
}


static void ProcessHotkey(UHK uhkHotkey)
{
	HWND hwnd = NULL; // RemoteGetFocus();
	HKL currentLayout = GetCurrentLayout(&hwnd);
	UHK uhkCurrentGroup = g_uhkCurrentGroup;
	LayoutsGuard guard;

	// Find the current keyboard layout's index
	std::map<HKL, KeyboardLayoutInfo>::iterator i_current
			= g_keyboardInfo.find(currentLayout);

	std::map<UHK, std::map<HKL, UHK>>::iterator
		i_hotkey = g_hotkeyInfo.find(uhkHotkey);
	if (i_hotkey == g_hotkeyInfo.end())
		return;

//	HKL hklSource = i_hotkey->first.bRecodeHK ? currentLayout : NULL;
	HKL hklSource = eHKTRecode == i_hotkey->first.btHKType
											? currentLayout : NULL;

	// action hotkeys
	if (eHKTGroup != i_hotkey->first.btHKType) {
		//if (i_hotkey->first.bCycleHK || i_hotkey->first.bRecodeHK) {
		switch (i_hotkey->first.btHKType) {
		case eHKTCycle:
		case eHKTRecode: {
			if (0 != g_uhkCurrentGroup.ulHotKey) {
				i_hotkey = g_hotkeyInfo.find(g_uhkCurrentGroup);
				if (i_hotkey != g_hotkeyInfo.end()
						&& !i_hotkey->second.empty())
					goto skip_to_group_hotkey; // continue as group hotkey
			}

			// iterate all layouts
			std::map<HKL, KeyboardLayoutInfo>::iterator	i = i_current;
			for (i++ ; i != i_current; i++) {
				if (i == g_keyboardInfo.end())
					i = g_keyboardInfo.begin();
				if (i->second.inUse)
					break;
			}

			SwitchKeyboardLayout(hwnd, hklSource, i->first);
			break;
		}
		//} else 	if (i_hotkey->first.bRemapHK) {
		case eHKTRemap:
			st_uhkInject = i_hotkey->second.begin()->second;
//			st_uhkInject.bRemapHK = 1;
			st_uhkInject.btHKType = eHKTRemap;
			if (NULL != st_hInjectEvent)
				SetEvent(st_hInjectEvent);
			break;
		//}	else 	if (i_hotkey->first.bEjectHK)
		case eHKTEject:
			EjectCD();
			break;
		}

		return;
	}

	// group hotkeys
	if (i_hotkey->second.empty())
		return;

	uhkCurrentGroup = uhkHotkey;

skip_to_group_hotkey:

	std::map<HKL, UHK>::iterator i_c = i_hotkey->second.find(i_current->first);

	std::map<HKL, UHK>::iterator i = i_hotkey->second.begin();
	if (i_c != i_hotkey->second.end())
		std::advance(i = i_c, 1);

	for (; i != i_c; i++) {
		if (i == i_hotkey->second.end())
			i = i_hotkey->second.begin();
		if (i->second.ulKey) // inUse
			break;
	}

	if (i == i_hotkey->second.end())
		return;

	if (g_uhkCurrentGroup.ulKey != uhkCurrentGroup.ulKey) {
		g_uhkCurrentGroup = uhkCurrentGroup;
		RegSaveGroup(); // force registry update
	}

	// switch keyboard layout
	SwitchKeyboardLayout(hwnd, hklSource, i->first/*hkl*/);
}


///////////////////////////////////////////////////////////////////////////////
// Selects the entire current line and converts it to the current kwyboard layout
void SwitchAndConvertThread(void* lParam)
{
	DWORD dwStart = 0;
	DWORD dwEnd = 0;
	HWND hwndFocus = RemoteGetFocus();
	DWORD_PTR dwMsgResult = 0;
	if (!SendMessageTimeout(hwndFocus, EM_GETSEL,
			reinterpret_cast<WPARAM>(&dwStart), reinterpret_cast<LPARAM>(&dwEnd),
			SMTO_ABORTIFHUNG | SMTO_BLOCK, 200, &dwMsgResult))
		return;

	if (dwStart >= dwEnd)
		SendKeyCombo(VK_CONTROL, 'A', TRUE);

	HKL* pHkls = (HKL*)lParam;
	if (NULL != pHkls && NULL != pHkls[0] && NULL != pHkls[1])
		ConvertSelectedTextInActiveWindow(pHkls[0], pHkls[1]);
}


unsigned __stdcall InjectInputThread(void*)
{
	do {
		int count = 0;
		INPUT input[(8 + 1) * 4] = { 0 };
		DWORD waitResult = WaitForSingleObject(st_hInjectEvent, INFINITE);
		if (WAIT_OBJECT_0 != waitResult)
			continue;
		if (st_bInjectThreadStop)
			break;

//		if (st_uhkInject.bRemapHK) {
		if (eHKTRemap == st_uhkInject.btHKType) {
			BYTE abtMods[] = { // 0 - "up" mods, 1 - "down" mods
				(0xFF & g_dwModifiers) & ~st_uhkInject.btMods,
				st_uhkInject.btMods & ~(0xFF & g_dwModifiers)
			};

			// "release" current and "press" new modifiers
			DWORD dwFlags = KEYEVENTF_KEYUP;
			for (int i = 0; i < sizeof(abtMods) / sizeof(*abtMods); i++) {
				for (int b = 0; b < 8; b++) {
					switch (abtMods[i] & (1 << b)) {
					case LEFT_LSHIFT:	input[count].ki.wVk = VK_LSHIFT;		break;
					case LEFT_RSHIFT:	input[count].ki.wVk = VK_RSHIFT;		break;
					case LEFT_LCTRL:	input[count].ki.wVk = VK_LCONTROL;	break;
					case LEFT_RCTRL:	input[count].ki.wVk = VK_RCONTROL;	break;
					case LEFT_LALT:		input[count].ki.wVk = VK_LMENU;			break;
					case LEFT_RALT:		input[count].ki.wVk = VK_RMENU;			break;
					case LEFT_LWIN:		input[count].ki.wVk = VK_LWIN;			break;
					case LEFT_RWIN:		input[count].ki.wVk = VK_RWIN;			break;
					case LEFT_MENU:		input[count].ki.wVk = VK_APPS;			break;
					default:
						continue;
					}

					input[count].ki.wScan = MapVirtualKey(input[count].ki.wVk, MAPVK_VK_TO_VSC);

					input[count].type = INPUT_KEYBOARD;
					input[count++].ki.dwFlags = dwFlags;
				}

				dwFlags = 0;
			}

			// inject hotkey's symbol
			input[count].type = INPUT_KEYBOARD;
			input[count].ki.wVk = st_uhkInject.btVK;
			input[count].ki.wScan = st_uhkInject.btSK;
			input[count++].ki.dwFlags = KEYEVENTF_EXTENDEDKEY;

			// restore "release" - "press" modifiers
			for (int i = count - 1; i >= 0; i--, count++) {
				input[count] = input[i];
				if (0 != (KEYEVENTF_KEYUP & input[count].ki.dwFlags))
					input[count].ki.dwFlags &= ~KEYEVENTF_KEYUP;
				else
					input[count].ki.dwFlags |= KEYEVENTF_KEYUP;
			}
			
			UINT u = ::SendInput(count, input, sizeof(INPUT));
			
			st_uhkInject.ulKey = 0;
		}

	} while (true);

	return 0;
}


#include <oleacc.h>

void ShowFadeOffIcon(bool bShow/*HWND hwndFocus /*= NULL* /*/)
{
	if (!bShow/*NULL == hwndFocus*/) {
		for (int i = 0; i < g_hwndOverlayWindows.size(); i++)
			SetWindowPos(g_hwndOverlayWindows[i], 0, 0, 0, 0, 0,
				SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE | SWP_HIDEWINDOW);
		return;
	}

/*
IAccessible* pAcc = NULL;
	VARIANT varChild;
	HRESULT hr = AccessibleObjectFromWindow(hwndFocus, idObject, IID_IAccessible, &pAcc);  
	if ((hr == S_OK) && (pAcc != NULL))
	{
		VARIANT varRole;
		hr = pAcc->get_accRole(varChild, &varRole);
		if ((hr == S_OK) && (varRole.vt == VT_I4) && (varRole.lVal == ROLE_SYSTEM_TEXT))
		{
			// ...
		}
		pAcc->Release();
	}
*/

/*	RECT rc = { 0 };
	GetWindowRect(hwndFocus, &rc);
	int height = rc.bottom - rc.top;
	int width = rc.right - rc.left;
	int dy = (height - 34) / 2;
	int dx = (width - 34) / 2;
	InflateRect(&rc, -dx, -dy);

	SetWindowPos(g_hwndMessageWindow, 0, rc.left, rc.top,
		rc.right - rc.left, rc.bottom - rc.top,
		SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW); */

	for (int i = 0; i < g_hwndOverlayWindows.size(); i++) {
		SetWindowPos(g_hwndOverlayWindows[i], HWND_TOPMOST, 0, 0, 0, 0,
			SWP_NOSIZE | SWP_NOMOVE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
		InvalidateRect(g_hwndOverlayWindows[i], NULL, TRUE);
	}

	bool bTimerActive = st_nOpacity > g_btFinishOpacity;
	st_nOpacity = g_btStartOpacity;
	if (!bTimerActive)
		SetTimer(g_hwndMessageWindow, 2, 10, FadeOffProc);
}


///////////////////////////////////////////////////////////////////////////////
// A LowLevelHookProc implementation that captures the CapsLock key
LRESULT CALLBACK LowLevelHookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
	KBDLLHOOKSTRUCT* data = (KBDLLHOOKSTRUCT*)lParam;

	if (nCode < 0 || NULL == data || 0 != (data->flags & LLKHF_INJECTED))
		goto skip_to_next_hook; // return CallNextHookEx(st_hHook, nCode, wParam, lParam);

	DWORD dwModifiers = VkToModBit(data->vkCode);

	BOOL bKeyDown = FALSE;
	switch (wParam) {
	case WM_KEYDOWN:
	case WM_SYSKEYDOWN:
		g_dwModifiers |= dwModifiers;
		bKeyDown = TRUE;
		break;

	case WM_KEYUP:
	case WM_SYSKEYUP:
		g_dwModifiers &= ~dwModifiers;
		break;

	default:
		goto skip_to_next_hook; // return CallNextHookEx(st_hHook, nCode, wParam, lParam);
	}

	UHK uhkHotkey(data->vkCode, data->scanCode, g_dwModifiers, false/*bKeyDown*/, 0 != (data->flags & LLKHF_EXTENDED));
	if (bKeyDown)
		g_uhkLastHotkey = uhkHotkey;

	g_dwKeysCount++;

#if 0
#ifdef _DEBUG
	TCHAR ch[1024] = { 0 };
	_stprintf(ch, _T("HK:%08x; VK:%08x SK:%08x (%08x) F:%08x P:%08x MOD:%08x Down:%s // %08x %08x\n"),
		uhkHotkey.ulKey, data->vkCode, data->scanCode, MapVirtualKey(data->vkCode, MAPVK_VK_TO_VSC),
		data->flags, wParam, g_dwModifiers, bKeyDown ? _T("down") : _T("up"), data->dwExtraInfo, GetKeyState(VK_APPS));
	OutputDebugString(ch);
#endif
#endif

	if (g_bCustomizingOn)
		goto skip_to_next_hook; // return CallNextHookEx(st_hHook, nCode, wParam, lParam);

	static UHK st_uhkHotkeyUp(0);

	BOOL change = FALSE;
	if (bKeyDown/*wParam == WM_KEYDOWN*/) {
#if 0
		for (std::map<UHK, std::map<HKL, UHK>>::iterator j = g_hotkeyInfo.begin();
				j != g_hotkeyInfo.end(); j++) {
			if (uhkHotkey.ulHotKey == j->first.ulHotKey) {
				st_uhkHotkeyUp = !j->first.bKeyDown ? uhkHotkey : 0;
				change = 0 == st_uhkHotkeyUp.ulHotKey;
				break;
			}
		}
#else
		std::map<UHK, std::map<HKL, UHK>>::iterator i = g_hotkeyInfo.find(uhkHotkey);
		if (i != g_hotkeyInfo.end()) {
			st_uhkHotkeyUp = !i->first.bKeyDown ? uhkHotkey : 0;
			change = 0 == st_uhkHotkeyUp.ulHotKey;
		}
#endif

		if (0 == dwModifiers)
			st_uhkHotkeyUp.ulHotKey = 0;
	}

#if 0
	TCHAR chName[1024] = { 0 };
	GetKeyNameText(data->scanCode << 16, chName, _countof(chName));
	OutputDebugString(chName);
	switch (wParam) {
	case WM_KEYDOWN: OutputDebugString(L" pressed\n"); break;
	case WM_KEYUP: OutputDebugString(L" released\n"); break;
	default:
		_stprintf(chName, _T("MSG:%08x\n"), wParam);
		OutputDebugString(chName); break;
	}
#endif

	// Handle CapsLock - only switch current layout
	if (change /*&& !st_dwHotkeyUp*/ /*&& !ctrl*/) {
		ProcessHotkey(uhkHotkey);
		return 1;
	}

	if (0 == g_dwModifiers && 0 != st_uhkHotkeyUp.ulKey) {
		ProcessHotkey(st_uhkHotkeyUp);
		st_uhkHotkeyUp.ulKey = 0;
		goto skip_to_next_hook; // return 1;
	}

skip_to_next_hook:
	return CallNextHookEx(st_hHook, nCode, wParam, lParam);
}
