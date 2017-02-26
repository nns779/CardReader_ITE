// debug.c

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <windows.h>

#if defined(_DEBUG) || defined(_DEBUG_MSG)

static bool enable = false;
static HANDLE _hFile = INVALID_HANDLE_VALUE;

void _dbg_write_W(const wchar_t *const str, const int c)
{
	OutputDebugStringW(str);

	if (_hFile != INVALID_HANDLE_VALUE)
	{
		char buf[0x800];
		int c2;

		c2 = WideCharToMultiByte(CP_ACP, 0, str, c, buf, 0x800, NULL, NULL);
		if (c2 > 0) {
			DWORD wb;
			WriteFile(_hFile, buf, c2, &wb, NULL);
		}
	}
}

void _dbg_write_A(const char *const str, const int c)
{
	OutputDebugStringA(str);

	if (_hFile != INVALID_HANDLE_VALUE) {
		DWORD wb;
		WriteFile(_hFile, str, c, &wb, NULL);
	}
}

#endif

void dbg_enable(const bool b)
{
#if defined(_DEBUG) || defined(_DEBUG_MSG)
	enable = b;
#endif
}

void dbg_open(const wchar_t *const path)
{
#if defined(_DEBUG) || defined(_DEBUG_MSG)
	if (enable == false)
		return;

	if (_hFile == INVALID_HANDLE_VALUE) {
		_hFile = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	}
#endif
}

void dbg_close()
{
#if defined(_DEBUG) || defined(_DEBUG_MSG)
	if (_hFile != INVALID_HANDLE_VALUE) {
		CloseHandle(_hFile);
		_hFile = INVALID_HANDLE_VALUE;
	}
#endif
}

void dbg_wprintf(const wchar_t *const format, ...)
{
#if defined(_DEBUG) || defined(_DEBUG_MSG)
	if (enable == false)
		return;

	va_list args;
	wchar_t buf[0x400];
	int c;

	va_start(args, format);
	c = vswprintf_s(buf, 0x400, format, args);
	va_end(args);
	if (c > 0) {
		_dbg_write_W(buf, c);
	}
#endif
}

void dbg_printf(const char *const format, ...)
{
#if defined(_DEBUG) || defined(_DEBUG_MSG)
	if (enable == false)
		return;

	va_list args;
	char buf[0x400];
	int c;

	va_start(args, format);
	c = vsprintf_s(buf, 0x400, format, args);
	va_end(args);
	if (c > 0) {
		_dbg_write_A(buf, c);
	}
#endif
}

void win32_err(const char *const str)
{
#if defined(_DEBUG) || defined(_DEBUG_MSG)
	if (enable == false)
		return;

	DWORD e = GetLastError();
	void *buf = NULL;

	if (FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, NULL, e, 0, (LPSTR)&buf, 64, NULL) > 0) {
		dbg_printf("%s: %s(%08X)\n", str, buf, e);
		LocalFree(buf);
	}
	else {
		dbg_printf("%s: %08X\n", str, e);
	}
#endif
}