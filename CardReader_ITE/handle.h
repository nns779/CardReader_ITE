// handle.h

#pragma once

#include <stdint.h>

typedef void(*handle_release_callback)(void *handle);
typedef void* handle_list;

extern bool handle_list_init(handle_list *const h, const uintptr_t base, const uintptr_t num, const handle_release_callback callback);
extern bool handle_list_deinit(handle_list h);
extern bool handle_list_put(handle_list h, void *const p, uintptr_t *const rv);
extern bool handle_list_get(handle_list h, const uintptr_t v, void **const rp);
extern bool handle_list_release(handle_list h, const uintptr_t v);
