#pragma once

#include <windows.h>
#include <string>

// UI language selection.  eAppLangSystem means "follow the Windows UI language".
enum EAppLanguage {
	eAppLangSystem = 0,
	eAppLangEnglish = 1,
	eAppLangRussian = 2
};

extern EAppLanguage g_eAppLanguage;

// Reads/writes the "Language" value under HKCU\Software\Recaps.
void LoadLanguageSetting();
void SaveLanguageSetting();

// Changes the UI language and persists it.
void SetAppLanguage(EAppLanguage lang);

// Returns the language id actually used for resources (never LANG_NEUTRAL).
LANGID GetEffectiveLangId();
bool IsRussianUi();

// Loads a string from the STRINGTABLE of the current UI language.
std::wstring Tr(UINT id);

// DialogBoxParam() equivalent that explicitly loads the dialog template for
// the selected UI language instead of relying on the process locale.
INT_PTR DialogBoxLocalized(HINSTANCE hInstance, UINT templateId,
		HWND hParent, DLGPROC lpDialogFunc, LPARAM dwInitParam);
