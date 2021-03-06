// string.c

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

bool wstrCompare(const wchar_t *const str1, const wchar_t *const str2)
{
	wchar_t *s1 = (wchar_t *)str1, *s2 = (wchar_t *)str2;

	while (*s1 && !(*s1 - *s2))
		s1++, s2++;

	return (!(*s1 - *s2)) ? true : false;
}

bool strCompare(const char *const str1, const char *const str2)
{
	char *s1 = (char *)str1, *s2 = (char *)str2;

	while (*s1 && !(*s1 - *s2))
		s1++, s2++;

	return (!(*s1 - *s2)) ? true : false;
}

bool wstrCompareEx(const wchar_t *const str1, const wchar_t *const str2, const wchar_t skip_ch)
{
	wchar_t *s1 = (wchar_t *)str1, *s2 = (wchar_t *)str2;

	while (*s1 && (!(*s1 - *s2) || !(*s2 - skip_ch)))
		s1++, s2++;

	return (!(*s1 - *s2)) ? true : false;
}

bool wstrCompareN(const wchar_t *const str1, const wchar_t *const str2, const size_t count)
{
	wchar_t *s1 = (wchar_t *)str1, *s2 = (wchar_t *)str2;
	size_t n = count;

	while (n && *s1 && !(*s1 - *s2)) {
		s1++, s2++; n--;
	}

	return (!n || !(*s1 - *s2)) ? true : false;
}

bool strCompareN(const char *const str1, const char *const str2, const size_t count)
{
	char *s1 = (char *)str1, *s2 = (char *)str2;
	size_t n = count;

	while (n && *s1 && !(*s1 - *s2)) {
		s1++, s2++; n--;
	}

	return (!n || !(*s1 - *s2)) ? true : false;
}

bool wstrMatch(const wchar_t *const str1, const wchar_t *const str2, const wchar_t skip_ch)
{
	wchar_t *s1 = (wchar_t *)str1, *s2 = (wchar_t *)str2;

	while (*s1)
	{
		if (!(*s2 - skip_ch))
		{
			s2++;

			while (*s2 && !(*s2 - skip_ch))
				s2++;

			if (!*s2) {
				return true;
			}

			while (*s1 && (*s1 - *s2))
				s1++;
		}

		wchar_t *t = s1;

		while (*s1 && !(*s1 - *s2))
			s1++, s2++;

		if (!(t - s1)) {
			break;
		}
		else if (!(*s1 - *s2)) {
			return true;
		}
	}

	return false;
}

uint32_t wstrLen(const wchar_t *const str)
{
	wchar_t *s = (wchar_t *)str;
	
	while (*s)
		s++;

	return (uint32_t)(s - str);
}

uint32_t strLen(const char *const str)
{
	char *s = (char *)str;

	while (*s)
		s++;

	return (uint32_t)(s - str);
}

void wstrCopy(wchar_t *const dst, const wchar_t *const src)
{
	wchar_t *d = (wchar_t *)dst, *s = (wchar_t *)src;

	do {
		*d = *s;
	} while (d++, *s++);

	return;
}

void wstrCopyN(wchar_t *const dst, const wchar_t *const src, const size_t count)
{
	size_t n = count;
	wchar_t *d = (wchar_t *)dst, *s = (wchar_t *)src;

	do {
		*d = *s;
	} while (n--, d++, (*s++ && n));

	while (n--)
		*d++ = L'\0';

	return;
}

bool wstrToUInt32(const wchar_t *const str, uint32_t *const ui32)
{
	wchar_t *s = (wchar_t *)str;
	uint32_t r = 0;

	if (!*s)
		return false;

	while (*s)
	{
		if (*s >= L'0' && *s <= L'9') {
			r *= 10;
			r += *s++ - L'0';
		}
		else {
			return false;
		}
	}

	*ui32 = r;

	return true;
}

bool strToUInt32(const char *const str, uint32_t *const ui32)
{
	char *s = (char *)str;
	uint32_t r = 0;

	if (!*s)
		return false;

	while (*s)
	{
		if (*s >= '0' && *s <= '9') {
			r *= 10;
			r += *s++ - '0';
		}
		else {
			return false;
		}
	}

	*ui32 = r;

	return true;
}

uint32_t wstrFromUInt32(wchar_t *const str, const size_t size, const uint32_t ui32, const int radix)
{
	uint32_t v = ui32;
	size_t n = size;
	wchar_t *d = (wchar_t *)str;

	if (radix < 2 || radix > 36 || n < 2)
		return 0;

	n--;

	do {
		d++;
		n--;
		v /= radix;
	} while (v && n);

	if (v && !n)
		/* no enough buffer */
		return 0;

	wchar_t *d2 = d;

	*d2 = L'\0';

	v = ui32;

	do {
		*--d2 = L"0123456789abcdefghijklmnopqrstuvwxyz"[v % radix];
		v /= radix;
	} while (v);

	return (uint32_t)(d - d2);
}

uint32_t strFromUInt32(char *const str, const size_t size, const uint32_t ui32, const int radix)
{
	uint32_t v = ui32;
	size_t n = size;
	char *d = (char *)str;

	if (radix < 2 || radix > 36 || n < 2)
		return 0;

	n--;

	do {
		d++;
		n--;
		v /= radix;
	} while (v && n);

	if (v && !n)
		/* no enough buffer */
		return 0;

	char *d2 = d;

	*d2 = '\0';

	v = ui32;

	do {
		*--d2 = "0123456789abcdefghijklmnopqrstuvwxyz"[v % radix];
		v /= radix;
	} while (v);

	return (uint32_t)(d - d2);
}

wchar_t * wstrGetWCharPtr(const wchar_t *const str, const wchar_t ch)
{
	wchar_t *p = (wchar_t *)str;

	while (*p && (*p - ch)) {
		p++;
	}

	return (*p - ch) ? NULL : p;
}
