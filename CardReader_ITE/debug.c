// debug.c

#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <windows.h>

void dbg_wprintf(const wchar_t *const format, ...)
{
	va_list args;
	wchar_t buf[0x100];

	va_start(args, format);
	vswprintf_s(buf, 0x100, format, args);
	OutputDebugStringW(buf);
	va_end(args);
}

void dbg_printf(const char *const format, ...)
{
	va_list args;
	char buf[0x100];

	va_start(args, format);
	vsprintf_s(buf, 0x100, format, args);
	OutputDebugStringA(buf);
	va_end(args);
}

void win32_err(const wchar_t *const str)
{
	DWORD e = GetLastError();
	void *buf = NULL;

	if (FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, NULL, e, 0, (LPWSTR)&buf, 64, NULL) > 0) {
		dbg_wprintf(L"%s: %s(%08X)\n", str, buf, e);
		LocalFree(buf);
	}
	else {
		dbg_wprintf(L"%s: %08X\n", str, e);
	}
}