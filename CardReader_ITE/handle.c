// handle.c

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "debug.h"
#include "memory.h"
#include "handle.h"

struct handle_list_info {
	uintptr_t base;
	uint32_t num;
	handle_release_callback callback;
	void *handle[1];
};

bool handle_list_init(handle_list *const h, const uintptr_t base, const uintptr_t num, const handle_release_callback callback)
{
	struct handle_list_info *info;

	if (num == 0 || callback == NULL)
		return false;

	info = memAlloc(sizeof(struct handle_list_info) + (sizeof(info->handle) * (num - 1)));
	if (info == NULL) {
		internal_err(L"handle_init: memAlloc failed");
		return false;
	}

	info->base = base;
	info->num = num;
	info->callback = callback;
	memset(info->handle, 0, sizeof(info->handle) * (num - 1));

	*h = info;

	return true;
}

bool handle_list_deinit(handle_list h)
{
	struct handle_list_info *info = h;

	for (uint32_t i = 0; i < info->num; i++) {
		if (info->handle[i] != NULL) {
			info->callback(info->handle[i]);
		}
	}

	memFree(info);

	return true;
}

bool handle_list_put(handle_list h, void *const p, uintptr_t *const rv)
{
	struct handle_list_info *info = h;
	uintptr_t id = 0, num = info->num;

	while (id < num)
	{
		if (info->handle[id] == NULL) {
			info->handle[id] = p;
			*rv = info->base + id;
			return true;
		}

		id++;
	}

	return false;
}

bool handle_list_get(handle_list h, const uintptr_t v, void **const rp)
{
	struct handle_list_info *info = h;
	uintptr_t id = (v - info->base);

	*rp = NULL;

	if (id >= info->num)
		return false;

	*rp = info->handle[id];

	return (*rp != NULL) ? true : false;
}

bool handle_list_release(handle_list h, const uintptr_t v)
{
	struct handle_list_info *info = h;
	uintptr_t id = (v - info->base);

	if (id >= info->num)
		return false;

	info->callback(info->handle[id]);
	info->handle[id] = NULL;

	return true;
}
