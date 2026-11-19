#include "stdafx.h"
#include "resource.h"
#include "localization.h"


#define BUFSIZE 2048


///////////////////////////////////////////////////////////////////////////////
// Shows a system error message
void ShowError(const WCHAR* message)
{
	WCHAR buffer[BUFSIZE];

	LPVOID errMessage = NULL;
	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER
		| FORMAT_MESSAGE_FROM_SYSTEM
		| FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL,
		GetLastError(),
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&errMessage,
		0,
		NULL
	);

	buffer[0] = '\0';
	wcscat_s(buffer, BUFSIZE, message);
	wcscat_s(buffer, BUFSIZE, L"\n\n");
	wcscat_s(buffer, BUFSIZE, Tr(IDS_MSG_ERROR_PREFIX).c_str());
	if (NULL != errMessage)
		wcscat_s(buffer, BUFSIZE, (const WCHAR*)errMessage);

	MessageBox(NULL, buffer, Tr(IDS_MSG_ERROR_TITLE).c_str(),
			MB_OK | MB_ICONINFORMATION);
	LocalFree(errMessage);
}


///////////////////////////////////////////////////////////////////////////////
// Prints an error message to the debugger
void PrintDebugString(const char* format, ...)
{
	char buffer[2048];

	va_list args;
	va_start(args, format);

	vsprintf_s(buffer, 2048, format, args);
	strcat_s(buffer, 2048, "\n");

	OutputDebugStringA(buffer);
}


void PrintDebugString(const WCHAR* format, ...)
{
	WCHAR buffer[2048];

	va_list args;
	va_start(args, format);

	vswprintf_s(buffer, 2048, format, args);
	wcscat_s(buffer, 2048, L"\n");

	OutputDebugStringW(buffer);
}

