// cfunc.c

#ifdef _RELEASE_LITE

#include <stdint.h>
#include <stddef.h>

#pragma function(memcpy)

void * memcpy(void *dst, const void *src, size_t size)
{
	size_t l;
#if 0
#ifdef _WIN64
	uint64_t *d = dst;
	const uint64_t *s = src;
#else
	uint32_t *d = dst;
	const uint32_t *s = src;
#endif

#ifdef _WIN64
	l = (size & (-1 ^ 0x07)) >> 3;
#else
	l = (size & (-1 ^ 0x03)) >> 2;
#endif

	while (l--) {
		*d++ = *s++;
	}

#ifdef _WIN64
	l = (size & 0x07);
#else
	l = (size & 0x03);
#endif
#else
	void *d = dst;
	const void *s = src;

	l = size;
#endif

	if (l)
	{
		uint8_t *d8 = (uint8_t *)d;
		const uint8_t *s8 = (const uint8_t *)s;

		while (l--) {
			*d8++ = *s8++;
		}
	}

	return dst;
}

#pragma function(memset)

void * memset(void *dst, int val, size_t size)
{
	uint8_t *d = dst;
	uint8_t v = val & 0xff;

	while (size--) {
		*d++ = v;
	}

	return dst;
}

#endif
