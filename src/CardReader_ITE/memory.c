// memory.c

#include <stdbool.h>
#include <windows.h>

HANDLE g_hHeap = NULL;

bool memInit()
{
	g_hHeap = HeapCreate(0, 0x1000, 0);

	return (g_hHeap != NULL) ? true : false;
}

void memDeinit()
{
	if (g_hHeap != NULL)
		HeapDestroy(g_hHeap);
}

void * memAlloc(const size_t size)
{
	return HeapAlloc(g_hHeap, HEAP_ZERO_MEMORY, size);
}

void memFree(const void *const p)
{
	HeapFree(g_hHeap, 0, (LPVOID)p);
}
