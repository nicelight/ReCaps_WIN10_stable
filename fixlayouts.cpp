#include "stdafx.h"
#include "fixlayouts.h"
#include "utils.h"


#define HKL_HEBREW  (HKL)0x040D040D
#define HKL_ENGLISH (HKL)0x04090409

static const int kClipboardRetries = 5;
static const DWORD kClipboardRetryDelayMs = 20;

static BOOL OpenClipboardWithRetry()
{
	for (int i = 0; i < kClipboardRetries; i++) {
		if (OpenClipboard(NULL))
			return TRUE;
		Sleep(kClipboardRetryDelayMs);
	}
	return FALSE;
}

///////////////////////////////////////////////////////////////////////////////
// Converts the text in the active window from one keyboard layout to another
// using the clipboard.
void ConvertSelectedTextInActiveWindow(HKL hklSource, HKL hklTarget)
{
	WCHAR* sourceText = NULL;
	WCHAR* targetText = NULL;

	// store previous clipboard data and set clipboard to dummy string
	ClipboardData prevClipboardData = { 0 };
	bool bClipboardStored = StoreClipboardData(&prevClipboardData);

	// SetClipboardText(dummy);

	DWORD dwSN = GetClipboardSequenceNumber();

	// copy the selected text by simulating Ctrl-C
	SendKeyCombo(VK_CONTROL, 'C', FALSE);

	for (int i = 0;
			dwSN == GetClipboardSequenceNumber() && i < 40; i++)
		Sleep(30);

	int nErr = dwSN != GetClipboardSequenceNumber() ? 0 : 2;
	bool bRestoreClipboard = 0 == nErr;

	// if Ctrl-C did not put anything new into the clipboard, do not convert
	// and paste the stale clipboard contents over the user's selection
	if (0 != nErr)
		goto cleanup;

	sourceText = GetClipboardText();
	if (NULL == sourceText || L'\0' == sourceText[0]) {
		nErr += 1;
		goto cleanup;
	}

	// if the string only matches one particular layout, use it
	// otherwise use the provided layout
	int matches = 0;
	HKL hklDetected = DetectLayoutFromString(sourceText, &matches);

//    PrintDebugString("Detected:%08x %d", hklDetected, matches);

	if (matches == 1)
		hklSource = hklDetected;

	// convert the text between layouts
	size_t length = wcslen(sourceText);
	targetText = (WCHAR*)malloc(sizeof(WCHAR) * (length + 1));
	if (NULL == targetText) {
		nErr += 1;
		goto cleanup;
	}
	size_t converted = LayoutConvertString(sourceText, targetText,
			length + 1, hklSource, hklTarget);

//  PrintDebugString(_T("Converted %d %08x -> %08x %s -> %s"),
//  converted, hklSource, hklTarget, sourceText, targetText);

	if (converted) {
		dwSN = GetClipboardSequenceNumber();
		// put the converted string on the clipboard
		if (SetClipboardText(targetText)) {
//      Sleep(100);
			for (int i = 0;
					dwSN == GetClipboardSequenceNumber() && i < 40; i++)
				Sleep(30);

			nErr += dwSN != GetClipboardSequenceNumber() ? 0 : 3;

			// simulate Ctrl-V to paste the text, replacing the previous text
			SendKeyCombo(VK_CONTROL, 'V', FALSE);

			// let the application complete pasting before
			// putting the old data back on the clipboard
			Sleep(REMOTE_APP_WAIT);
		}
	}

	// restore the original clipboard data
cleanup:
	if (bClipboardStored) {
		if (bRestoreClipboard)
			RestoreClipboardData(&prevClipboardData);
		FreeClipboardData(&prevClipboardData);
	}

	// free allocated memory
	free(sourceText);
	free(targetText);

#if 1 // testing !
	for (int i = 0; i < nErr; i++) {
		MessageBeep(MB_ICONINFORMATION);
		Sleep(300);
	}
#endif
}


///////////////////////////////////////////////////////////////////////////////
// Converts a character from one keyboard layout to another
WCHAR LayoutConvertChar(WCHAR ch, HKL hklSource, HKL hklTarget)
{
	if (NULL == hklSource || NULL == hklTarget)
		return 0;

	// special handling for some ambivalent characters in hebrew layout
	if (hklSource == HKL_HEBREW && hklTarget == HKL_ENGLISH) {
		switch (ch) {
			case L'.':  return L'/';
			case L'/':  return L'q';
			case L'\'': return L'w';
			case L',':  return L'\'';
		}

	} else if (hklSource == HKL_ENGLISH && hklTarget == HKL_HEBREW) {
		switch (ch) {
			case L'/':  return L'.';
			case L'q':  return L'/';
			case L'w':  return L'\'';
			case L'\'': return L',';
		}
	}

	// get the virtual key code and the shift state using the
	// character and the source keyboard layout
	SHORT vkAndShift = VkKeyScanEx(ch, hklSource);
	if (vkAndShift == -1)
		return 0; // no such char in source keyboard layout

	BYTE vk = LOBYTE(vkAndShift);
	BYTE shift = HIBYTE(vkAndShift);

	// convert the shift state returned from VkKeyScanEx
	// to an array that represents the key state usable
	// with ToUnicodeEx that we'll be calling next
	BYTE keyState[256] = { 0 };
	if (shift & 1) keyState[VK_SHIFT] = 0x80;	// turn on high bit
	if (shift & 2) keyState[VK_CONTROL] = 0x80;
	if (shift & 4) keyState[VK_MENU] = 0x80;

	// convert virtual key and key state to a new character
	// using the target keyboard layout
	WCHAR buffer[10] = { 0 };
	int result = ToUnicodeEx(vk, 0, keyState, buffer, 10, 0, hklTarget);

	// result can be more than 1 if the character in the source
	// layout is represented by several characters in the target
	// layout, but we ignore this to simplify the function.
	if (result == 1 || result == -1)
		return buffer[0];

	// conversion failed for some reason
	return 0;
}


///////////////////////////////////////////////////////////////////////////////
// Converts a string from one keyboard layout to another
size_t LayoutConvertString(const WCHAR* str, WCHAR* buffer,
		size_t size, HKL hklSource, HKL hklTarget)
{
	if (NULL == str || NULL == buffer || 0 == size)
		return 0;

	size_t length = wcslen(str);
	if (length >= size)
		length = size - 1;

	size_t i;
	for (i = 0; i < length; i++) {
		WCHAR ch = LayoutConvertChar(str[i], hklSource, hklTarget);
		if (ch == 0)
			return 0;

		buffer[i] = ch;
	}

	buffer[i] = '\0';

	return i;
}


///////////////////////////////////////////////////////////////////////////////
// Goes through all the installed keyboard layouts and returns a layout that
// can generate the string. If not matching layout is found, returns NULL.
// If `multiple` isn't NULL it will be set to the number of matched layouts.
HKL DetectLayoutFromString(const WCHAR* str, int* pmatches)
{
	HKL result = NULL;
	int matches = 0;

	int layoutCount = GetKeyboardLayoutList(0, NULL);
	if (layoutCount <= 0 || NULL == str) {
		if (pmatches)
			*pmatches = 0;
		return NULL;
	}

	std::vector<HKL> hkls(layoutCount);
	int loaded = GetKeyboardLayoutList(layoutCount, &hkls[0]);
	if (loaded <= 0) {
		if (pmatches)
			*pmatches = 0;
		return NULL;
	}

	size_t length = wcslen(str);
	for (int layout = 0; layout < loaded; layout++) {
		BOOL validLayout = TRUE;
		for (size_t i = 0; i < length; i++) {
			SHORT vk = VkKeyScanEx(str[i], hkls[layout]);
			if (vk == -1) {
				validLayout = FALSE;
				break;
			}
		}

		if (validLayout) {
			matches++;
			if (!result)
				result = hkls[layout];
		}
	}

	if (pmatches)
		*pmatches = matches;

	return result;
}


///////////////////////////////////////////////////////////////////////////////
// Stores the clipboard data in all its formats in `formats`.
// You must call FreeAllClipboardData on `formats` when it's no longer needed.
BOOL StoreClipboardData(ClipboardData* formats)
{
	if (NULL == formats)
		return FALSE;

	formats->count = 0;
	formats->dataArray = NULL;

	if (!OpenClipboardWithRetry())
		return FALSE;

	int count = CountClipboardFormats();
	ClipboardFormat* dataArray = NULL;
	if (count > 0) {
		dataArray = (ClipboardFormat*)calloc(
				count, sizeof(ClipboardFormat));
		if (NULL == dataArray) {
			CloseClipboard();
			return FALSE;
		}
	}

	int i = 0;
	UINT format = EnumClipboardFormats(0);
	while (format && i < count) {
		HANDLE dataHandle = GetClipboardData(format);
		LPVOID source = NULL;
		if (NULL != dataHandle
				&& NULL != (source = GlobalLock(dataHandle))) {
			SIZE_T size = GlobalSize(dataHandle);
			if (size > 0) {
				HANDLE copy = GlobalAlloc(GHND, size);
				if (NULL != copy) {
					LPVOID dest = GlobalLock(copy);
					if (NULL != dest) {
						CopyMemory(dest, source, size);
						GlobalUnlock(copy);
						dataArray[i].format = format;
						dataArray[i].dataHandle = copy;
					} else
						GlobalFree(copy);
				}
			}
			GlobalUnlock(dataHandle);
		}

		// next format
		format = EnumClipboardFormats(format);
		i++;
	}

	CloseClipboard();

	formats->count = count;
	formats->dataArray = dataArray;
	return TRUE;
}


///////////////////////////////////////////////////////////////////////////////
// Restores the data in the clipboard from `formats` that was generated by
// StoreClipboardData.
BOOL RestoreClipboardData(ClipboardData* formats)
{
	if (NULL == formats || formats->count <= 0 || NULL == formats->dataArray)
		return FALSE;

	if (OpenClipboardWithRetry()) {
		EmptyClipboard();
		for (int i = 0; i < formats->count; i++) {
			ClipboardFormat& entry = formats->dataArray[i];
			if (NULL == entry.dataHandle || 0 == entry.format)
				continue;

			// SetClipboardData takes ownership only on success
			if (NULL == SetClipboardData(entry.format, entry.dataHandle))
				GlobalFree(entry.dataHandle);

			entry.dataHandle = NULL;
		}

		CloseClipboard();

		return TRUE;
	}

	return FALSE;
}


///////////////////////////////////////////////////////////////////////////////
// Frees `formats` allocated by StoreClipboardData
void FreeClipboardData(ClipboardData* formats)
{
	if (NULL == formats || NULL == formats->dataArray)
		return;

	// release any copies that were not handed over to the clipboard
	for (int i = 0; i < formats->count; i++)
		if (NULL != formats->dataArray[i].dataHandle)
			GlobalFree(formats->dataArray[i].dataHandle);

	free(formats->dataArray);
	formats->dataArray = NULL;
	formats->count = 0;
}


///////////////////////////////////////////////////////////////////////////////
// Gets unicode text from the clipboard.
// You must free the returned string when you don't need it anymore.
WCHAR* GetClipboardText()
{
	WCHAR* text = NULL;
	if (!OpenClipboardWithRetry())
		return NULL;

	HANDLE handle = GetClipboardData(CF_UNICODETEXT);
	if (NULL == handle) {
		CloseClipboard();
		return NULL;
	}

	const WCHAR* clipboardText = (const WCHAR*)GlobalLock(handle);
	if (NULL == clipboardText) {
		CloseClipboard();
		return NULL;
	}

	// do not trust the data to be null-terminated: bound the scan by its size
	SIZE_T bytes = GlobalSize(handle);
	size_t maxChars = bytes / sizeof(WCHAR);
	size_t length = 0;
	while (length < maxChars && L'\0' != clipboardText[length])
		length++;

	text = (WCHAR*)malloc(sizeof(WCHAR) * (length + 1));
	if (NULL != text) {
		if (length > 0)
			memcpy(text, clipboardText, sizeof(WCHAR) * length);
		text[length] = L'\0';
	}

	GlobalUnlock(handle);
	CloseClipboard();

	return text;
}


///////////////////////////////////////////////////////////////////////////////
// Puts unicode text on the clipboard
BOOL SetClipboardText(const WCHAR* text)
{
	if (NULL == text)
		return FALSE;

	if (!OpenClipboardWithRetry())
		return FALSE;

	size_t size = sizeof(WCHAR) * (wcslen(text) + 1);
	HANDLE handle = GlobalAlloc(GHND, size);
	if (NULL == handle) {
		CloseClipboard();
		return FALSE;
	}

	WCHAR* clipboardText = (WCHAR*)GlobalLock(handle);
	if (NULL == clipboardText) {
		GlobalFree(handle);
		CloseClipboard();
		return FALSE;
	}

	memcpy(clipboardText, text, size);

	GlobalUnlock(handle);

	if (!EmptyClipboard()
			|| NULL == SetClipboardData(CF_UNICODETEXT, handle)) {
		GlobalFree(handle);
		CloseClipboard();
		return FALSE;
	}

	CloseClipboard();

	return TRUE;
}


///////////////////////////////////////////////////////////////////////////////
// Simulates a key press in the active window
void SendKey(BYTE vk, BOOL extended)
{
	keybd_event(vk, 0, extended ? KEYEVENTF_EXTENDEDKEY : 0, 0);
	keybd_event(vk, 0,
			KEYEVENTF_KEYUP | (extended ? KEYEVENTF_EXTENDEDKEY : 0), 0);
}


///////////////////////////////////////////////////////////////////////////////
// Simulates a key combination (such as Ctrl+X) in the active window
void SendKeyCombo(BYTE vkModifier, BYTE vk, BOOL extended)
{
	BOOL modPressed = (GetKeyState(vkModifier) & 0x80000000) > 0;
#if 0 
	if (!modPressed)
		keybd_event(vkModifier, 0, 0, 0);

	keybd_event(vk, 0, extended ? KEYEVENTF_EXTENDEDKEY : 0, 0);
	if (!modPressed)
		keybd_event(vkModifier, 0, KEYEVENTF_KEYUP, 0);

	keybd_event(vk, 0,
			KEYEVENTF_KEYUP | (extended ? KEYEVENTF_EXTENDEDKEY : 0), 0);
#else
	int count = 0;
	INPUT input[] = { 
		{ INPUT_KEYBOARD, 0 },
		{ INPUT_KEYBOARD, 0 },
		{ INPUT_KEYBOARD, 0 },
		{ INPUT_KEYBOARD, 0 },
	};

	if (!modPressed)
		input[count++].ki.wVk = vkModifier;

	input[count].ki.wVk = vk;
	input[count++].ki.dwFlags = extended ? KEYEVENTF_EXTENDEDKEY : 0;

	if (!modPressed) {
		input[count].ki.wVk = vkModifier;
		input[count++].ki.dwFlags = KEYEVENTF_KEYUP;
	}

	input[count].ki.wVk = vk;
	input[count++].ki.dwFlags = KEYEVENTF_KEYUP | (extended ? KEYEVENTF_EXTENDEDKEY : 0);

	UINT u = ::SendInput(count, input, sizeof(INPUT));
/*#ifdef _DEBUG
		_stprintf(ch, _T("SendInput:%d %d\n"), u, GetLastError());
		OutputDebugString(ch);
#endif*/

#endif

}
