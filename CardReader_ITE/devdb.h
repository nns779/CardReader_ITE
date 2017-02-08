// devdb.h

#pragma once

#include <stdint.h>
#include <windows.h>

#define DEVDB_MAX_DEV_NUM		8
#define DEVDB_MAX_DEV_REF_NUM	16
#define DEVDB_MAX_PATH_SIZE		512
#define DEVDB_MAX_NAME_SIZE		128

#pragma pack(1)

struct devdb_shared_devinfo
{
	wchar_t path[DEVDB_MAX_PATH_SIZE];
	uint32_t ref;
	uint32_t clear;
	uint8_t user[4];
};

struct devdb_shared_info
{
	uint32_t signature;

	uint32_t lock;
	uint32_t available;
	uint32_t waiting;

	uint32_t count;
	uint32_t size;
	struct devdb_shared_devinfo dev[1];
};

#pragma pack()

typedef struct _devdb
{
	HANDLE ev;
	HANDLE shmem;
	struct devdb_shared_info *info;
	wchar_t name[DEVDB_MAX_NAME_SIZE];
} devdb;

typedef enum
{
	DEVDB_S_OK = 0,
	DEVDB_E_INTERNAL,
	DEVDB_E_INVALID_PARAMETER,
	DEVDB_E_NO_MEMORY,
	DEVDB_E_API,
	DEVDB_E_INTERNAL_LIMIT,
	DEVDB_E_NO_DEVICES,
	DEVDB_E_DEVICE_NOT_FOUND,
} devdb_status_t;

typedef int(*devdb_enum_callback)(devdb *const db, const uint32_t id, const struct devdb_shared_devinfo *const devinfo, void *prm);

extern devdb_status_t devdb_open(devdb *const db, const wchar_t *const name, const uint32_t user_size);
extern devdb_status_t devdb_close(devdb *const db);
extern void devdb_lock(devdb *const db);
extern void devdb_unlock(devdb *const db);
extern devdb_status_t devdb_update_nolock(devdb *const db);
extern devdb_status_t devdb_update(devdb *const db);
extern devdb_status_t devdb_enum_nolock(devdb *const db, const devdb_enum_callback callback, void *prm);
extern devdb_status_t devdb_enum(devdb *const db, const devdb_enum_callback callback, void *prm);
extern devdb_status_t devdb_get_shared_devinfo_nolock(devdb *const db, const uint32_t id, struct devdb_shared_devinfo **const devinfo);
extern devdb_status_t devdb_get_shared_devinfo(devdb *const db, const uint32_t id, struct devdb_shared_devinfo **const devinfo);
extern devdb_status_t devdb_get_path_nolock(devdb *const db, const uint32_t id, const wchar_t **const path);
extern devdb_status_t devdb_get_path(devdb *const db, const uint32_t id, const wchar_t **const path);
extern devdb_status_t devdb_get_ref_count_nolock(devdb *const db, const uint32_t id, uint32_t *const ref);
extern devdb_status_t devdb_get_ref_count(devdb *const db, const uint32_t id, uint32_t *const ref);
extern devdb_status_t devdb_get_userdata_nolock(devdb *const db, const uint32_t id, void **pp);
extern devdb_status_t devdb_get_userdata(devdb *const db, const uint32_t id, void **pp);
extern devdb_status_t devdb_ref_nolock(devdb *const db, const uint32_t id, uint32_t *const ref);
extern devdb_status_t devdb_ref(devdb *const db, const uint32_t id, uint32_t *const ref);
extern devdb_status_t devdb_unref_nolock(devdb *const db, const uint32_t id, const bool clear, uint32_t *const ref);
extern devdb_status_t devdb_unref(devdb *const db, const uint32_t id, const bool clear, uint32_t *const ref);

#define devdb_v_name(devdb) ((devdb)->name)
