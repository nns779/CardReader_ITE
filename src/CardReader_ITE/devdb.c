// devdb.c

#include <stdbool.h>
#include <stdint.h>
#include <windows.h>
#include <SetupAPI.h>

#include "debug.h"
#include "memory.h"
#include "string.h"
#include "devdb.h"
#include "devdb_userdef.h"

#pragma comment(lib, "SetupAPI.lib")

static const wchar_t event_name[] = L"devdb_" DEVDB_UNIQUE_NAME L"_event_";
static const wchar_t shmem_name[] = L"devdb_" DEVDB_UNIQUE_NAME L"_shmem_";
static const wchar_t devdb_guid[] = L"_{5EFFECB0-706A-4ED7-B944-9C65B3BE1722}";

#define DEVDB_SHARED_INFO_SIGNATURE	0x935FBC8A

#define make_obj_name(buf, name1, name1_len, name2) \
	memcpy((buf), (name2), sizeof((name2)) - (1 * sizeof(wchar_t))); \
	memcpy((buf) + ((sizeof((name2)) / sizeof(wchar_t)) - 1), (name1), (name1_len) * sizeof(wchar_t)); \
	memcpy((buf) + ((sizeof((name2)) / sizeof(wchar_t)) - 1) + (name1_len), devdb_guid, sizeof(devdb_guid))

devdb_status_t devdb_open(devdb *const db, const wchar_t *const name, const wchar_t *const id, const uint32_t user_size)
{
	uint32_t name_len;
	uint32_t id_len;

	name_len = wstrLen(name);
	if (name_len >= DEVDB_MAX_NAME_SIZE) {
		internal_err("devdb_open: name_len >= DEVDB_MAX_NAME_SIZE");
		return DEVDB_E_INVALID_PARAMETER;
	}

	id_len = wstrLen(id);
	if (id_len >= DEVDB_MAX_ID_SIZE) {
		internal_err("devdb_open: id_len >= DEVDB_MAX_ID_SIZE");
		return DEVDB_E_INVALID_PARAMETER;
	}

	devdb_status_t r = DEVDB_E_INTERNAL;
	wchar_t obj_name[256];

	// event

	HANDLE ev;

	make_obj_name(obj_name, name, name_len, event_name);
	dbg("devdb_open: obj_name(1): %ws", obj_name);

	ev = CreateEventW(NULL, FALSE, FALSE, obj_name);
	if (ev == NULL) {
		win32_err("devdb_open: CreateEventW");
		r = DEVDB_E_API;
		goto end1;
	}

	// shared memory

	HANDLE shmem;
	uint32_t devinfo_size, shmem_size;
	DWORD le;
	struct devdb_shared_info *info;

	make_obj_name(obj_name, name, name_len, shmem_name);
	dbg("devdb_open: obj_name(2): %ws", obj_name);

	devinfo_size = (sizeof(struct devdb_shared_devinfo) - sizeof(((struct devdb_shared_devinfo *)NULL)->user) + user_size);
	shmem_size = (sizeof(struct devdb_shared_info) - sizeof(struct devdb_shared_devinfo)) + (devinfo_size * DEVDB_MAX_DEV_NUM);

	shmem = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, shmem_size, obj_name);
	if (shmem == NULL) {
		win32_err("devdb_open: CreateFileMappingW");
		r = DEVDB_E_API;
		goto end2;
	}

	le = GetLastError();

	info = MapViewOfFile(shmem, FILE_MAP_WRITE, 0, 0, 0);
	if (info == NULL) {
		win32_err("devdb_open: MapViewOfFile");
		r = DEVDB_E_API;
		goto end3;
	}

	if (le != ERROR_ALREADY_EXISTS)
	{
		info->lock = 0;
		info->available = 1;
		info->waiting = 0;

		info->count = DEVDB_MAX_DEV_NUM;
		info->size = devinfo_size;
		memset(info->dev, 0, (devinfo_size * DEVDB_MAX_DEV_NUM));

		InterlockedExchange(&info->signature, DEVDB_SHARED_INFO_SIGNATURE);
	}
	else
	{
		while (info->signature == 0) {
			Sleep(0);
		}
		if (info->signature != DEVDB_SHARED_INFO_SIGNATURE) {
			internal_err("devdb_open: incorrect signature");
			r = DEVDB_E_INTERNAL;
			goto end4;
		}
		else if (info->size != devinfo_size) {
			internal_err("devdb_open: couldn't use it");
			r = DEVDB_E_INTERNAL;
			goto end4;
		}
	}

	db->ev = ev;
	db->shmem = shmem;
	db->info = info;
	memcpy(db->name, name, (name_len + 1) * sizeof(wchar_t));
	memcpy(db->id, id, (id_len + 1) * sizeof(wchar_t));

	return DEVDB_S_OK;

end4:
	UnmapViewOfFile(info);
end3:
	CloseHandle(shmem);
end2:
	CloseHandle(ev);
end1:
	return r;
}

devdb_status_t devdb_close(devdb *const db)
{
	if (db->info != NULL) {
		UnmapViewOfFile(db->info);
		db->info = NULL;
	}

	if (db->shmem != NULL) {
		CloseHandle(db->shmem);
		db->shmem = NULL;
	}

	if (db->ev != NULL) {
		CloseHandle(db->ev);
		db->ev = NULL;
	}

	return DEVDB_S_OK;
}

static void _devdb_spin_lock(devdb *const db)
{
	while (InterlockedExchange(&db->info->lock, 1))
		Sleep(0);
}

static void _devdb_spin_unlock(devdb *const db)
{
	InterlockedExchange(&db->info->lock, 0);
}

void devdb_lock(devdb *const db)
{
	dbg("devdb_lock");

	while (1)
	{
		_devdb_spin_lock(db);

		if (db->info->available > 0) {
			db->info->available--;
			_devdb_spin_unlock(db);
			return;
		}

		db->info->waiting++;
		_devdb_spin_unlock(db);

		WaitForSingleObject(db->ev, INFINITE);
	}
}

void devdb_unlock(devdb *const db)
{
	dbg("devdb_unlock");

	_devdb_spin_lock(db);

	db->info->available++;
	if (db->info->waiting) {
		db->info->waiting--;
		SetEvent(db->ev);
	}

	_devdb_spin_unlock(db);

	return;
}

bool _devdb_parse_interface_path(const wchar_t *const path, wchar_t *const id)
{
	wchar_t *p;

	p = wstrGetWCharPtr(path, L'\\');
	if (p == NULL) {
		return false;
	}

	if (wstrCompareN(p, L"\\\\?\\", 4) == false) {
		return false;
	}

	// usb#vid_vvvv
	p = wstrGetWCharPtr(p + 4, L'#');
	if (p == NULL) {
		return false;
	}

	// &pid_pppp#ssssss
	p = wstrGetWCharPtr(p + 1, L'#');
	if (p == NULL || *++p == L'#') {
		return false;
	}

	wchar_t *p2;

	// ssssss#{xxxxxxxx
	p2 = wstrGetWCharPtr(p + 1, L'#');
	if (p2 == NULL) {
		return false;
	}

	if ((p2 - p) >= DEVDB_MAX_ID_SIZE)
		return false;

	// ssssss
	wstrCopyN(id, p, (p2 - p));

	return true;
}

devdb_status_t devdb_update_nolock(devdb *const db)
{
	dbg("devdb_update_nolock");

	HDEVINFO devInfo;

	devInfo = SetupDiGetClassDevsW(&(const GUID) { DEVDB_DEVICE_CLASS }, NULL, NULL, DIGCF_DEVICEINTERFACE | DIGCF_PRESENT);
	if (devInfo == INVALID_HANDLE_VALUE) {
		win32_err("devdb_update_nolock: SetupDiGetClassDevsW");
		return DEVDB_E_API;
	}

	PSP_DEVICE_INTERFACE_DETAIL_DATA_W *detailDataArray;
	uint32_t detailDataIndex = 0, detailDataMaxIndex = db->info->count - 1;

	detailDataArray = memAlloc(sizeof(PSP_DEVICE_INTERFACE_DETAIL_DATA_W) * db->info->count);
	if (detailDataArray == NULL) {
		internal_err("devdb_update_nolock: memAlloc failed 1");
		SetupDiDestroyDeviceInfoList(devInfo);
		return DEVDB_E_NO_MEMORY;
	}

	SP_DEVICE_INTERFACE_DATA interfaceData;
	uint32_t i = 0;
	devdb_status_t r = DEVDB_E_NO_DEVICES;

	interfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

	while (SetupDiEnumDeviceInterfaces(devInfo, NULL, &(const GUID) { DEVDB_DEVICE_CLASS }, i, &interfaceData) == TRUE)
	{
		PSP_DEVICE_INTERFACE_DETAIL_DATA_W detailData;

		detailData = memAlloc(sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W) + sizeof(WCHAR) * (512 - ANYSIZE_ARRAY));
		if (detailData == NULL) {
			internal_err("devdb_update_nolock: memAlloc failed 2");
			r = DEVDB_E_NO_MEMORY;
			break;
		}

		detailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

		if (SetupDiGetDeviceInterfaceDetailW(devInfo, &interfaceData, detailData, sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W) + sizeof(WCHAR) * (512 - ANYSIZE_ARRAY), NULL, NULL) == FALSE) {
			win32_err("devdb_update_nolock: SetupDiGetDeviceInterfaceDetailW");
			memFree(detailData);
			i++;
			continue;
		}

		HKEY hKey;

		hKey = SetupDiOpenDeviceInterfaceRegKey(devInfo, &interfaceData, 0, KEY_READ);
		if (hKey == INVALID_HANDLE_VALUE) {
			win32_err("devdb_update_nolock: SetupDiOpenDeviceInterfaceRegKey");
			memFree(detailData);
			i++;
			continue;
		}

		wchar_t fn[128];
		DWORD type = REG_NONE, size = sizeof(fn);
		LSTATUS ls;

		ls = RegQueryValueExW(hKey, L"FriendlyName", NULL, &type, (LPBYTE)fn, &size);
		RegCloseKey(hKey);

		if (ls != ERROR_SUCCESS) {
			win32_err("devdb_update_nolock: RegQueryValueExW");
		}
		else if (type != REG_SZ) {
			internal_err("devdb_update_nolock: RegQueryValueExW: type != REG_SZ");
		}
		else
		{
			if (wstrCompare(fn, db->name) == false) {
				memFree(detailData);
				i++;
				continue;
			}

			if (detailDataIndex >= detailDataMaxIndex) {
				internal_err("devdb_update_nolock: internal limit");
				memFree(detailData);
				break;
			}

			detailDataArray[detailDataIndex++] = detailData;
			i++;

			r = DEVDB_S_OK;
			continue;
		}

		memFree(detailData);
		i++;
	}

	SetupDiDestroyDeviceInfoList(devInfo);

	{
		struct devdb_shared_info *info = db->info;
		uint8_t *p = (uint8_t *)info->dev;
		uint32_t c = info->count, s = info->size;

		for (uint32_t j = 0; j < c; j++, p += s)
		{
			struct devdb_shared_devinfo *devinfo = (struct devdb_shared_devinfo *)p;
			uint32_t k;

			if (wstrIsEmpty(devinfo->path))
				continue;

			for (k = 0; k < detailDataIndex; k++)
			{
				if (detailDataArray[k] != NULL && wstrCompare(detailDataArray[k]->DevicePath, devinfo->path) == true) {
					// システム上に存在する(利用可能)
					devinfo->available = 1;
					memFree(detailDataArray[k]);
					detailDataArray[k] = NULL;
					break;
				}
			}

			if (k == detailDataIndex) {
				// システム上に存在しない
				if (devinfo->ref > 0) {
					// 開かれているが利用できない
					devinfo->available = 0;
				}
				else {
					// 開かれていない
					memset(devinfo, 0, s);
				}
			}
		}
	}

	{
		uint32_t lid = 0;

		for (uint32_t j = 0; j < detailDataIndex; j++)
		{
			if (detailDataArray[j] == NULL)
				continue;

			struct devdb_shared_info *info = db->info;
			uint8_t *p = (uint8_t *)info->dev;
			uint32_t c = info->count, s = info->size;

			for (uint32_t k = lid; k < c; k++)
			{
				struct devdb_shared_devinfo *devinfo = (struct devdb_shared_devinfo *)p;

				if (wstrIsEmpty(devinfo->path))
				{
					// 空きエントリ

					if (_devdb_parse_interface_path(detailDataArray[j]->DevicePath, devinfo->id) == true) {
						// 利用可能
						wstrCopyN(devinfo->path, detailDataArray[j]->DevicePath, 512);
						devinfo->available = 1;
						lid = k + 1;
					}
					else {
						dbg("devdb_update_nolock: _devdb_parse_interface_path failed");
					}
					memFree(detailDataArray[j]);
					detailDataArray[j] = NULL;
					break;
				}

				p += s;
			}

			if (detailDataArray[j] != NULL) {
				// 空きがない
				memFree(detailDataArray[j]);
				detailDataArray[j] = NULL;
				lid = c;
			}
		}
	}

	memFree(detailDataArray);

	return r;
}

devdb_status_t devdb_update(devdb *const db)
{
	devdb_status_t r;

	devdb_lock(db);
	r = devdb_update_nolock(db);
	devdb_unlock(db);

	return r;
}

#define _devdb_get_shared_devinfo(db, id) ((struct devdb_shared_devinfo *)(((uint8_t *)((db)->info->dev)) + ((db)->info->size * (id))))
#define _devdb_is_valid_devinfo(db, devinfo) (!wstrIsEmpty((devinfo)->path) && (wstrIsEmpty((db)->id) || wstrCompareEx((devinfo)->id, (db)->id, L'*') == true) && ((devinfo)->available != 0))

devdb_status_t devdb_enum_nolock(devdb *const db, const devdb_enum_callback callback, void *prm)
{
	devdb_status_t r = DEVDB_E_NO_DEVICES;

	for (uint32_t i = 0, c = db->info->count; i < c; i++)
	{
		struct devdb_shared_devinfo *devinfo = _devdb_get_shared_devinfo(db, i);

		if (_devdb_is_valid_devinfo(db, devinfo))
		{
			r = DEVDB_S_OK;

			if (callback(db, i, devinfo, prm) == 0)
				break;
		}
	}

	return r;
}

devdb_status_t devdb_enum(devdb *const db, const devdb_enum_callback callback, void *prm)
{
	devdb_status_t r;

	devdb_lock(db);
	r = devdb_enum_nolock(db, callback, prm);
	devdb_unlock(db);

	return r;
}

devdb_status_t devdb_get_shared_devinfo_nolock(devdb *const db, const uint32_t id, struct devdb_shared_devinfo **const devinfo)
{
	*devinfo = NULL;

	if (db->info->count <= id) {
		return DEVDB_E_INVALID_PARAMETER;
	}

	struct devdb_shared_devinfo *di;

	di = _devdb_get_shared_devinfo(db, id);

	if (!_devdb_is_valid_devinfo(db, di))
	{
		devdb_status_t r;

		r = devdb_update_nolock(db);
		if (r != DEVDB_S_OK)
			return r;

		if (!_devdb_is_valid_devinfo(db, di)) {
			return DEVDB_E_DEVICE_NOT_FOUND;
		}
	}

	*devinfo = di;

	return DEVDB_S_OK;
}

devdb_status_t devdb_get_shared_devinfo(devdb *const db, const uint32_t id, struct devdb_shared_devinfo **const devinfo)
{
	devdb_status_t r;

	devdb_lock(db);
	r = devdb_get_shared_devinfo_nolock(db, id, devinfo);
	devdb_unlock(db);

	return r;
}

devdb_status_t devdb_get_path_nolock(devdb *const db, const uint32_t id, const wchar_t **const path)
{
	*path = NULL;

	if (db->info->count <= id) {
		return DEVDB_E_INVALID_PARAMETER;
	}

	*path = _devdb_get_shared_devinfo(db, id)->path;

	return DEVDB_S_OK;
}

devdb_status_t devdb_get_path(devdb *const db, const uint32_t id, const wchar_t **const path)
{
	devdb_status_t r;

	devdb_lock(db);
	r = devdb_get_path_nolock(db, id, path);
	devdb_unlock(db);

	return r;
}

devdb_status_t devdb_get_ref_count_nolock(devdb *const db, const uint32_t id, uint32_t *const ref)
{
	if (db->info->count <= id) {
		return DEVDB_E_INVALID_PARAMETER;
	}

	*ref = _devdb_get_shared_devinfo(db, id)->ref;

	return DEVDB_S_OK;
}

devdb_status_t devdb_get_ref_count(devdb *const db, const uint32_t id, uint32_t *const ref)
{
	devdb_status_t r;

	devdb_lock(db);
	r = devdb_get_ref_count_nolock(db, id, ref);
	devdb_unlock(db);

	return r;
}

devdb_status_t devdb_get_userdata_nolock(devdb *const db, const uint32_t id, void **pp)
{
	*pp = NULL;

	if (db->info->count <= id) {
		return DEVDB_E_INVALID_PARAMETER;
	}

	*pp = _devdb_get_shared_devinfo(db, id)->user;

	return DEVDB_S_OK;
}

devdb_status_t devdb_get_userdata(devdb *const db, const uint32_t id, void **pp)
{
	devdb_status_t r;

	devdb_lock(db);
	r = devdb_get_userdata_nolock(db, id, pp);
	devdb_unlock(db);

	return r;
}

devdb_status_t devdb_ref_nolock(devdb *const db, const uint32_t id, uint32_t *const ref)
{
	struct devdb_shared_devinfo *devinfo;

	if (db->info->count <= id) {
		return DEVDB_E_INVALID_PARAMETER;
	}

	devinfo = _devdb_get_shared_devinfo(db, id);

	if (devinfo->ref > DEVDB_MAX_DEV_REF_NUM) {
		internal_err("devdb_ref: ref > DEVDB_MAX_DEV_REF_NUM");
		return DEVDB_E_INTERNAL;
	}
	else if (devinfo->ref == DEVDB_MAX_DEV_REF_NUM) {
		internal_err("devdb_ref: ref == DEVDB_MAX_DEV_REF_NUM");
		return DEVDB_E_INTERNAL_LIMIT;
	}

	devinfo->ref++;

	if (ref != NULL)
		*ref = devinfo->ref;

	return DEVDB_S_OK;
}

devdb_status_t devdb_ref(devdb *const db, const uint32_t id, uint32_t *const ref)
{
	devdb_status_t r;

	devdb_lock(db);
	r = devdb_ref_nolock(db, id, ref);
	devdb_unlock(db);

	return r;
}

devdb_status_t devdb_unref_nolock(devdb *const db, const uint32_t id, uint32_t *const ref)
{
	struct devdb_shared_devinfo *devinfo;

	if (db->info->count <= id) {
		return DEVDB_E_INVALID_PARAMETER;
	}

	devinfo = _devdb_get_shared_devinfo(db, id);

	if (devinfo->ref > DEVDB_MAX_DEV_REF_NUM) {
		internal_err("devdb_unref: ref > DEVDB_MAX_DEV_REF_NUM");
		return DEVDB_E_INTERNAL;
	}
	else if (devinfo->ref == DEVDB_MAX_DEV_REF_NUM) {
		internal_err("devdb_unref: unref == 0");
		return DEVDB_E_INTERNAL_LIMIT;
	}

	devinfo->ref--;

	if (ref != NULL)
		*ref = devinfo->ref;

	return DEVDB_S_OK;
}

devdb_status_t devdb_unref(devdb *const db, const uint32_t id, uint32_t *const ref)
{
	devdb_status_t r;

	devdb_lock(db);
	r = devdb_unref_nolock(db, id, ref);
	devdb_unlock(db);

	return r;
}
