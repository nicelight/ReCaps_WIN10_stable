#include "stdafx.h"
#include "resource.h"
#include "recaps.h"
#include "localization.h"


EAppLanguage g_eAppLanguage = eAppLangSystem;

static const LANGID kEnglishLangId =
		MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US);
static const LANGID kRussianLangId =
		MAKELANGID(LANG_RUSSIAN, SUBLANG_RUSSIAN_RUSSIA);


static HRSRC FindLocaleResource(HINSTANCE hInstance, LPCTSTR type,
		LPCTSTR name, LANGID langid)
{
	HRSRC hrsrc = FindResourceEx(hInstance, type, name, langid);
	if (NULL == hrsrc)
		hrsrc = FindResourceEx(hInstance, type, name, kEnglishLangId);
	if (NULL == hrsrc)
		hrsrc = FindResourceEx(hInstance, type, name,
				MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL));
	return hrsrc;
}


void LoadLanguageSetting()
{
	HKEY hKey = NULL;
	DWORD value = 0;
	DWORD type = 0;
	DWORD size = sizeof(value);

	if (ERROR_SUCCESS == RegOpenKeyEx(HKEY_CURRENT_USER,
			_T("Software\\Recaps"), 0, KEY_QUERY_VALUE, &hKey)) {
		if (ERROR_SUCCESS == RegQueryValueEx(hKey, _T("Language"), NULL,
				&type, reinterpret_cast<BYTE*>(&value), &size)
				&& REG_DWORD == type
				&& sizeof(value) == size
				&& value <= eAppLangRussian)
			g_eAppLanguage = static_cast<EAppLanguage>(value);

		RegCloseKey(hKey);
	}
}


void SaveLanguageSetting()
{
	HKEY hKey = NULL;

	if (ERROR_SUCCESS == RegCreateKeyEx(HKEY_CURRENT_USER,
			_T("Software\\Recaps"), 0, NULL, 0, KEY_SET_VALUE, NULL,
			&hKey, NULL)) {
		DWORD value = static_cast<DWORD>(g_eAppLanguage);
		RegSetValueEx(hKey, _T("Language"), 0, REG_DWORD,
				reinterpret_cast<const BYTE*>(&value), sizeof(value));
		RegCloseKey(hKey);
	}
}


LANGID GetEffectiveLangId()
{
	if (eAppLangEnglish == g_eAppLanguage)
		return kEnglishLangId;

	if (eAppLangRussian == g_eAppLanguage)
		return kRussianLangId;

	// Follow the Windows UI language; Russian is the only translation for now,
	// everything else falls back to English.
	if (LANG_RUSSIAN == PRIMARYLANGID(GetUserDefaultUILanguage()))
		return kRussianLangId;

	return kEnglishLangId;
}


bool IsRussianUi()
{
	return LANG_RUSSIAN == PRIMARYLANGID(GetEffectiveLangId());
}


void SetAppLanguage(EAppLanguage lang)
{
	g_eAppLanguage = lang;
	SaveLanguageSetting();

	// Keep OS-provided strings (file dialogs etc.) consistent with the UI.
	SetThreadUILanguage(GetEffectiveLangId());
}


std::wstring Tr(UINT id)
{
	if (NULL == g_hInstance)
		return std::wstring();

	UINT blockId = (id >> 4) + 1;
	UINT index = id & 0xF;

	HRSRC hrsrc = FindLocaleResource(g_hInstance, RT_STRING,
			MAKEINTRESOURCE(blockId), GetEffectiveLangId());
	if (NULL == hrsrc)
		return std::wstring();

	HGLOBAL hres = LoadResource(g_hInstance, hrsrc);
	if (NULL == hres)
		return std::wstring();

	const BYTE* data = static_cast<const BYTE*>(LockResource(hres));
	DWORD size = SizeofResource(g_hInstance, hrsrc);
	if (NULL == data || 0 == size)
		return std::wstring();

	const BYTE* end = data + size;
	for (UINT i = 0; i < index; i++) {
		if (data + sizeof(WORD) > end)
			return std::wstring();

		WORD length = *reinterpret_cast<const WORD*>(data);
		data += sizeof(WORD);
		if (data + length * sizeof(WCHAR) > end)
			return std::wstring();

		data += length * sizeof(WCHAR);
	}

	if (data + sizeof(WORD) > end)
		return std::wstring();

	WORD length = *reinterpret_cast<const WORD*>(data);
	data += sizeof(WORD);
	if (data + length * sizeof(WCHAR) > end)
		return std::wstring();

	return std::wstring(reinterpret_cast<const WCHAR*>(data), length);
}


INT_PTR DialogBoxLocalized(HINSTANCE hInstance, UINT templateId,
		HWND hParent, DLGPROC lpDialogFunc, LPARAM dwInitParam)
{
	HRSRC hrsrc = FindLocaleResource(hInstance, RT_DIALOG,
			MAKEINTRESOURCE(templateId), GetEffectiveLangId());
	if (NULL == hrsrc)
		return -1;

	HGLOBAL hres = LoadResource(hInstance, hrsrc);
	if (NULL == hres)
		return -1;

	const DLGTEMPLATE* pTemplate =
			static_cast<const DLGTEMPLATE*>(LockResource(hres));
	if (NULL == pTemplate)
		return -1;

	return DialogBoxIndirectParam(hInstance, pTemplate, hParent,
			lpDialogFunc, dwInitParam);
}
