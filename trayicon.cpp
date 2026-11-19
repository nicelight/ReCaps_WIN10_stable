#include "stdafx.h"
#include "trayicon.h"


void AddTrayIcon(HWND hWnd, UINT uID,
		UINT uCallbackMsg, HICON hIcon, LPTSTR pszToolTip)
{
	NOTIFYICONDATA  nid     = {0};
	nid.cbSize              = sizeof(nid);
	nid.hWnd                = hWnd;
	nid.uID                 = uID;
	nid.uFlags              = NIF_ICON | NIF_MESSAGE | NIF_TIP;
	nid.uCallbackMessage    = uCallbackMsg;
	nid.hIcon               = hIcon;
	wcsncpy_s(nid.szTip, _countof(nid.szTip), pszToolTip, _TRUNCATE);
	Shell_NotifyIcon(NIM_ADD, &nid);
}


void ModifyTrayIcon(
		HWND hWnd, UINT uID, HICON hIcon, LPCTSTR pszToolTip)
{
	NOTIFYICONDATA  nid = {0};
	nid.cbSize  = sizeof(nid);
	nid.hWnd    = hWnd;
	nid.uID     = uID;

	if (hIcon != NULL) {
		nid.hIcon = hIcon;
		nid.uFlags |= NIF_ICON;
	}

	if (pszToolTip) {
		wcsncpy_s(nid.szTip, _countof(nid.szTip), pszToolTip, _TRUNCATE);
		nid.uFlags |= NIF_TIP;
	}

	if (hIcon != NULL || pszToolTip)
		Shell_NotifyIcon(NIM_MODIFY, &nid);
}


void RemoveTrayIcon(HWND hWnd, UINT uID)
{
	NOTIFYICONDATA nid = {0};
	nid.cbSize  = sizeof( nid );
	nid.hWnd    = hWnd;
	nid.uID     = uID;
	Shell_NotifyIcon( NIM_DELETE, &nid );
}

