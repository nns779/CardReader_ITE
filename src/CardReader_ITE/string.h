// string.h

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

extern bool wstrCompare(const wchar_t *const str1, const wchar_t *const str2);
extern bool strCompare(const char *const str1, const char *const str2);
extern bool wstrCompareEx(const wchar_t *const str1, const wchar_t *const str2, const wchar_t skip_ch);
extern bool wstrCompareN(const wchar_t *const str1, const wchar_t *const str2, const size_t count);
extern bool strCompareN(const char *const str1, const char *const str2, const size_t count);
extern bool wstrMatch(const wchar_t *const str1, const wchar_t *const str2, const wchar_t skip_ch);
extern uint32_t wstrLen(const wchar_t *const str);
extern uint32_t strLen(const char *const str);
extern void wstrCopy(wchar_t *const dst, const wchar_t *const src);
extern void wstrCopyN(wchar_t *const dst, const wchar_t *const src, const size_t count);
extern bool wstrToUInt32(const wchar_t *const str, uint32_t *const ui32);
extern bool strToUInt32(const char *const str, uint32_t *const ui32);
extern uint32_t wstrFromUInt32(wchar_t *const str, const size_t size, const uint32_t ui32, const int radix);
extern uint32_t strFromUInt32(char *const str, const size_t size, const uint32_t ui32, const int radix);
extern wchar_t * wstrGetWCharPtr(const wchar_t *const str, const wchar_t ch);

#define wstrIsEmpty(str) (((str)[0]) == L'\0')
