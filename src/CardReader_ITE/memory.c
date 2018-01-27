// memory.c

#include <stdbool.h>
#include <windows.h>

static HANDLE _hHeap = NULL;

bool memInit()
{
	_hHeap = HeapCreate(0, 0x1000, 0);

	return (_hHeap != NULL) ? true : false;
}

void memDeinit()
{
	if (_hHeap != NULL)
		HeapDestroy(_hHeap);
}

void * memAlloc(const size_t size)
{
	return HeapAlloc(_hHeap, HEAP_ZERO_MEMORY, size);
}

void memFree(const void *const p)
{
	HeapFree(_hHeap, 0, (LPVOID)p);
}
