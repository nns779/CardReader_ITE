// debug.h

#pragma once

extern void dbg_open(const wchar_t *const path);
extern void dbg_close();
extern void dbg_wprintf(const wchar_t *const format, ...);
extern void dbg_printf(const char *const format, ...);
extern void win32_err(const char *const str);

#define dbg(msg, ...)	dbg_printf(msg "\n", ##__VA_ARGS__)
#define dbgW(msg, ...)	dbg_wprintf(msg L"\n", ##__VA_ARGS__)
#define internal_err dbg
