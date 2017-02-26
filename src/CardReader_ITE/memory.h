// memory.h

#pragma once

#include <stdbool.h>

extern bool memInit();
extern void memDeinit();
extern void * memAlloc(const size_t size);
extern void memFree(const void *const p);
