// itecard.c

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <windows.h>
#include <SetupAPI.h>
#include <ks.h>
#include <ksmedia.h>
#include <bdatypes.h>
#include <bdamedia.h>

#include "debug.h"
#include "memory.h"
#include "string.h"
#include "itecard.h"
#include "ite.h"

#pragma comment(lib, "SetupAPI.lib")

static const wchar_t mutex_name[] = L"itecard_db_shmem_mutex %s {6635D7C8-6CDE-42F5-A868-7D4E283B402A}";
static const wchar_t shmem_name[] = L"itecard_db_shmem %s {22E92268-2BD0-4A7F-B5ED-4D81473CF3FE}";

#define ITECARD_DB_SIGNATURE_0	0xA1BF436E
#define ITECARD_DB_SIGNATURE	0xC3EE7A80

#define itecard_db_check_signature(card) ((card)->signature == ITECARD_DB_SIGNATURE)

//--------------------------------
// database functions
//--------------------------------

static itecard_status_t _itecard_db_open(struct itecard_db_handle *const db, const wchar_t *const name)
{
	wchar_t obj_name[256];
	HANDLE mutex;

	swprintf_s(obj_name, 256, mutex_name, name);
	
	mutex = CreateMutexW(NULL, TRUE, obj_name);
	if (mutex == NULL) {
		win32_err(L"_itecard_db_open: CreateMutexW");
		goto end1;
	}
	else if (GetLastError() == ERROR_ALREADY_EXISTS) {
		WaitForSingleObject(mutex, INFINITE);
	}

	HANDLE shmem;
	DWORD le;

	swprintf_s(obj_name, 256, shmem_name, name);

	shmem = CreateFileMappingW(NULL, NULL, PAGE_READWRITE, 0, sizeof(struct itecard_db), obj_name);
	if (shmem == NULL) {
		win32_err(L"_itecard_db_open: CreateFileMappingW");
		goto end2;
	}

	le = GetLastError();

	struct itecard_db *card;

	card = MapViewOfFile(shmem, FILE_MAP_WRITE, 0, 0, 0);
	if (card == NULL) {
		win32_err(L"_itecard_db_open: MapViewOfFile");
		goto end3;
	}

	if (le != ERROR_ALREADY_EXISTS)
	{
		card->signature = ITECARD_DB_SIGNATURE;
		card->count = ITECARD_MAX_DEV_NUM;
		card->size = sizeof(struct itecard_db_dev);
		memset(card->dev, 0, sizeof(struct itecard_db_dev) * ITECARD_MAX_DEV_NUM);
	}
	else if (!itecard_db_check_signature(card)) {
		internal_err(L"_itecard_db_open: itecard_db_check failed");
		goto end4;
	}

	db->mutex = mutex;
	db->shmem = shmem;
	db->card = card;

	ReleaseMutex(mutex);

	return ITECARD_S_OK;

end4:
	UnmapViewOfFile(card);
end3:
	CloseHandle(shmem);
end2:
	ReleaseMutex(mutex);
	CloseHandle(mutex);
end1:
	return ITECARD_E_INTERNAL;
}

static itecard_status_t _itecard_db_close(struct itecard_db_handle *const db)
{
	if (db->card != NULL) {
		UnmapViewOfFile(db->card);
		db->card = NULL;
	}

	if (db->shmem != NULL) {
		CloseHandle(db->shmem);
		db->shmem = NULL;
	}

	if (db->mutex != NULL) {
		CloseHandle(db->mutex);
		db->mutex = NULL;
	}

	return ITECARD_S_OK;
}

static itecard_status_t _itecard_db_lock(struct itecard_db_handle *const db)
{
	WaitForSingleObject(db->mutex, INFINITE);
	return ITECARD_S_OK;
}

static itecard_status_t _itecard_db_unlock(struct itecard_db_handle *const db)
{
	ReleaseMutex(db->mutex);
	return ITECARD_S_OK;
}

typedef int(*_itecard_db_update_callback)(struct itecard_db_handle *const db, const uint32_t id, void *prm);

static itecard_status_t _itecard_db_update(struct itecard_db_handle *const db, uint32_t *const index, const wchar_t *const friendlyName, const _itecard_db_update_callback callback, void *callback_prm)
{
	if (index == NULL || friendlyName == NULL || callback == NULL)
		return ITECARD_E_INVALID_PARAMETER;

	_itecard_db_lock(db);

	HDEVINFO devInfo;

	devInfo = SetupDiGetClassDevsW(&(const GUID) {STATIC_KSCATEGORY_BDA_NETWORK_TUNER}, NULL, NULL, DIGCF_DEVICEINTERFACE | DIGCF_PRESENT);
	if (devInfo == INVALID_HANDLE_VALUE) {
		win32_err(L"_itecard_db_update: SetupDiGetClassDevsW");
		_itecard_db_unlock(db);
		return ITECARD_E_FATAL;
	}

	SP_DEVICE_INTERFACE_DATA interfaceData;
	uint32_t i = *index;
	itecard_status_t r = ITECARD_E_NO_MORE_DATA;
	uint32_t lid = 0;

	interfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

	while (SetupDiEnumDeviceInterfaces(devInfo, NULL, &(const GUID) {STATIC_KSCATEGORY_BDA_NETWORK_TUNER}, i, &interfaceData) == TRUE)
	{
		PSP_DEVICE_INTERFACE_DETAIL_DATA_W detailData;

		detailData = memAlloc(sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W) + sizeof(WCHAR) * (512 - ANYSIZE_ARRAY));
		if (detailData == NULL) {
			win32_err(L"_itecard_db_update: HeapAlloc");
			r = ITECARD_E_NO_MEMORY;
			break;
		}

		detailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

		if (SetupDiGetDeviceInterfaceDetailW(devInfo, &interfaceData, detailData, sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W) + sizeof(WCHAR) * (512 - ANYSIZE_ARRAY), NULL, NULL) == FALSE) {
			win32_err(L"_itecard_db_update: SetupDiGetDeviceInterfaceDetailW");
			memFree(detailData);
			i++;
			continue;
		}

		HKEY hKey;

		hKey = SetupDiOpenDeviceInterfaceRegKey(devInfo, &interfaceData, 0, KEY_READ);
		if (hKey == INVALID_HANDLE_VALUE) {
			win32_err(L"_itecard_db_update: SetupDiOpenDeviceInterfaceRegKey");
			memFree(detailData);
			i++;
			continue;
		}

		wchar_t fn[128];
		DWORD type = REG_NONE, size = sizeof(fn);
		LSTATUS ls;

		ls = RegQueryValueEx(hKey, L"FriendlyName", NULL, &type, (LPBYTE)fn, &size);
		CloseHandle(hKey);
		if (ls != ERROR_SUCCESS) {
			win32_err(L"_itecard_db_update: RegQueryValueExW");
		}
		else if (type != REG_SZ) {
			internal_err(L"_itecard_db_update: RegQueryValueExW: type != REG_SZ");
		}
		else
		{
			if (wstrCompare(fn, friendlyName) == false) {
				memFree(detailData);
				i++;
				continue;
			}

			struct itecard_db *card_db = db->card;
			uint8_t *p = (uint8_t *)card_db->dev;
			uint32_t c = card_db->count, s = card_db->size, id = 0;
			bool new_device = false;

			for (uint32_t k = 0; k < c; k++)
			{
				struct itecard_db_dev *dev = (struct itecard_db_dev *)p;

				if (dev->path[0] != L'\0')
				{
					if (dev->ref == 0) {
						dev->path[0] = L'\0';
					}
					else if (wstrCompare(detailData->DevicePath, dev->path) == true)
					{
						if (dev->ref >= ITECARD_MAX_DEV_REF_COUNT) {
							internal_err(L"_itecard_db_update: ref limit");
						}
						else {
							id = k + 1;
						}
						break;
					}
				}

				if (id == 0 && dev->path[0] == L'\0' && (k + 1) > lid) {
					id = k + 1;
					lid = id;
					new_device = true;
				}

				p += s;
			}

			if (id != 0)
			{
				int r2 = 0;

				id--;

				// < 0: skip
				// 0: return
				// 1: register and return
				// 2: register
				if (r2 = callback(db, id, callback_prm) >= 0)
				{
					if (r2 > 0 && new_device == true) {
						wstrCopy(card_db->dev[id].path, detailData->DevicePath);
					}

					if (r2 < 2) {
						*index = i;

						memFree(detailData);
						SetupDiDestroyDeviceInfoList(devInfo);
						_itecard_db_unlock(db);

						return ITECARD_S_OK;
					}
				}
			}
			else {
				internal_err(L"_itecard_db_update: internal limit");
				memFree(detailData);
				r = ITECARD_E_INTERNAL_LIMIT;
				break;
			}
		}

		memFree(detailData);
		i++;
	}

	SetupDiDestroyDeviceInfoList(devInfo);
	_itecard_db_unlock(db);

	*index = 0;
	return r;
}

static itecard_status_t _itecard_db_query_by_id_nolock(struct itecard_db_handle *const db, const uint32_t id, struct itecard_db_dev **const dev)
{
	if (db->card->count > id) {
		*dev = (struct itecard_db_dev *)(((uint8_t *)db->card->dev) + (db->card->size * id));
		return ITECARD_S_OK;
	}

	return ITECARD_E_NO_DATA;
}

static itecard_status_t _itecard_db_ref_dev_nolock(struct itecard_db_handle *const db, struct itecard_db_dev *const dev)
{
	itecard_status_t r = ITECARD_E_INTERNAL;

	if (dev->ref > ITECARD_MAX_DEV_REF_COUNT) {
		internal_err(L"_itecard_db_ref_dev_nolock: fatal error");
	}
	else if (dev->ref == ITECARD_MAX_DEV_REF_COUNT) {
		internal_err(L"_itecard_db_ref_dev_nolock: limit");
		r = ITECARD_E_INTERNAL_LIMIT;
	}
	else {
		dev->ref++;
		r = ITECARD_S_OK;
	}

	return r;
}

static itecard_status_t _itecard_db_unref_dev_nolock(struct itecard_db_handle *const db, struct itecard_db_dev *const dev)
{
	itecard_status_t r = ITECARD_E_INTERNAL;

	if (dev->ref > ITECARD_MAX_DEV_REF_COUNT) {
		internal_err(L"_itecard_db_unref_dev_nolock: fatal error");
	}
	else if (dev->ref == 0) {
		internal_err(L"_itecard_db_unref_dev_nolock: limit");
		r = ITECARD_E_INTERNAL_LIMIT;
	}
	else {
		dev->ref--;
		if (dev->ref == 0) {
			dev->path[0] = L'\0';
		}
		r = ITECARD_S_OK;
	}

	return r;
}

//--------------------------------
// devlist functions
//--------------------------------

itecard_status_t itecard_devlist_open(struct itecard_devlist *const devlist, const wchar_t *const name)
{
	itecard_status_t r = ITECARD_E_INTERNAL, ret;
	size_t n_len;

	n_len = wstrLen(name);
	if (n_len >= 128) {
		internal_err(L"itecard_devlist_open: name is too large.");
		goto end0;
	}
	else if (n_len == 0) {
		internal_err(L"itecard_devlist_open: name is empty.");
		goto end0;
	}

	struct itecard_db_handle *db = &devlist->db;

	ret = _itecard_db_open(db, name);
	if (ret != ITECARD_S_OK) {
		internal_err(L"itecard_devlist_open: _itecard_db_open failed");
		r = ret;
		goto end0;
	}

	devlist->signature = ITECARD_DEVLIST_SIGNATURE;
	memcpy(devlist->name, name, n_len * sizeof(wchar_t));

	r = ITECARD_S_OK;

end0:
	return r;
}

itecard_status_t itecard_devlist_close(struct itecard_devlist *const devlist)
{
	_itecard_db_close(&devlist->db);
	devlist->signature = 0;

	return ITECARD_S_OK;
}

struct devlist_enum_data
{
	itecard_devlist_enum_callback callback;
	struct itecard_devlist *devlist;
	void *prm;
};

static int devlist_enum_callback(struct itecard_db_handle *const db, const uint32_t id, void *prm)
{
	struct devlist_enum_data *d = prm;

	return (d->callback(d->devlist, id, db->card->dev[id].path, d->prm) == 1) ? -1 : 0;
}

itecard_status_t itecard_devlist_enum(struct itecard_devlist *const devlist, const itecard_devlist_enum_callback callback, void *prm)
{
	if (callback == NULL)
		return ITECARD_E_INVALID_PARAMETER;

	struct devlist_enum_data d;
	uint32_t idx = 0;

	d.devlist = devlist;
	d.callback = callback;
	d.prm = prm;

	return _itecard_db_update(&devlist->db, &idx, devlist->name, devlist_enum_callback, &d);
}

static int devlist_register_callback(struct itecard_db_handle *const db, const uint32_t id, void *prm)
{
	uint32_t target_id = *(uint32_t *)prm;

	if (id == target_id) {
		return 1;
	}

	return -1;
}

itecard_status_t itecard_devlist_register(struct itecard_devlist *const devlist, const uint32_t id)
{
	uint32_t prm = id;
	uint32_t idx = 0;
	itecard_status_t ret;

	ret = _itecard_db_update(&devlist->db, &idx, devlist->name, devlist_register_callback, &prm);
	if (ret == ITECARD_E_NO_MORE_DATA)
		return ITECARD_E_NO_DEVICE;

	return ret;
}

//--------------------------------
// card functions
//--------------------------------

#define microSleep(microseconds) Sleep((microseconds) / 1000 + 1)

itecard_status_t itecard_open(struct itecard_handle *const handle, const wchar_t *const name, const uint32_t id, const itecard_protocol_t protocol, const bool exclusive/*, itecard_protocol_t *const active_protocol*/)
{
	itecard_status_t r = ITECARD_E_INTERNAL, ret;
	struct itecard_db_handle *const db = &handle->db;

	// open database
	ret = _itecard_db_open(db, name);
	if (ret != ITECARD_S_OK) {
		internal_err(L"itecard_open: _itecard_db_open failed");
		r = ret;
		goto end0;
	}

	_itecard_db_lock(db);

	struct itecard_db_dev *dev;

	// query from database
	ret = _itecard_db_query_by_id_nolock(db, id, &dev);
	if (ret != ITECARD_S_OK) {
		internal_err(L"itecard_open: _itecard_db_query_by_id_nolock failed");
		r = ret;
		goto end1;
	}

	// check path
	if (dev->path[0] == '\0') {
		// invalid
		internal_err(L"itecard_open: device path is null");
		goto end1;
	}

	ite_dev *ite = &handle->ite;

	// init device instance
	if (ite_init(ite) == false) {
		internal_err(L"itecard_open: ite_init failed");
		goto end1;
	}

	ite_lock(ite);

	// open device
	if (ite_open(ite, dev->path) == false) {
		internal_err(L"itecard_open: ite_open failed");
		r = ITECARD_E_NO_DEVICE;
		goto end2;
	}

	if (protocol != ITECARD_PROTOCOL_UNDEFINED)
	{
		if (dev->exclusive == true) {
			internal_err(L"itecard_open: not allowed");
			r = ITECARD_E_NOT_SHARED;
			goto end2;
		}

		if (exclusive == true) {
			if (dev->ref != 0) {
				internal_err(L"itecard_open: device was opened in share mode by another application");
				r = ITECARD_E_SHARED;
				goto end2;
			}
			dev->exclusive = true;
		}
	}

	ret = _itecard_db_ref_dev_nolock(db, dev);
	if (ret != ITECARD_S_OK) {
		internal_err(L"itecard_open: _itecard_db_ref_dev_nolock failed");
		r = ret;
		goto end2;
	}

	handle->signature = ITECARD_SIGNATURE;
	handle->id = id;
	handle->exclusive = (exclusive == true) ? true : false;
	handle->protocol = protocol;
	handle->dev = dev;
	
	ite_unlock(ite);
	_itecard_db_unlock(db);

	return ITECARD_S_OK;

end2:
	ite_close(ite);
	ite_unlock(ite);
	ite_release(ite);
end1:
	_itecard_db_unlock(db);
	_itecard_db_close(db);
end0:
	return r;
}

itecard_status_t itecard_close(struct itecard_handle *const handle, const bool reset)
{
	ite_close(&handle->ite);
	ite_release(&handle->ite);

	_itecard_db_lock(&handle->db);

	if (handle->exclusive == true) {
		handle->dev->exclusive = false;
	}

	if (handle->dev != NULL) {
		_itecard_db_unref_dev_nolock(&handle->db, handle->dev);
		if (reset == true || handle->dev->reset == true) {
			if (handle->dev->ref == 0) {
				card_clear(&handle->dev->card);
				handle->dev->reset = false;
			}
			else {
				handle->dev->reset = true;
			}
		}
		handle->dev = NULL;
	}

	_itecard_db_unlock(&handle->db);
	_itecard_db_close(&handle->db);

	handle->signature = 0;

	return ITECARD_S_OK;
}

static itecard_status_t _itecard_detect_nolock(struct itecard_handle *const handle, bool *const b)
{
	struct ite_devctl_data d;

	d.code = ITE_DEVCTL_CARD_DETECT;

	if (ite_devctl_nolock(&handle->ite, ITE_IOCTL_IN, &d) == false) {
		internal_err(L"_itecard_detect_nolock: ite_devctl_nolock failed");
		return ITECARD_E_FAILED;
	}

	*b = (d.card_present == 0) ? false : true;

	return ITECARD_S_OK;
}

static itecard_status_t _itecard_reset_nolock(struct itecard_handle *const handle)
{
	struct ite_devctl_data d;

	d.code = ITE_DEVCTL_CARD_RESET;

	if (ite_devctl_nolock(&handle->ite, ITE_IOCTL_OUT, &d) == false) {
		internal_err(L"_itecard_reset_nolock: ite_devctl_nolock failed");
		return ITECARD_E_FAILED;
	}

	return ITECARD_S_OK;
}

static itecard_status_t _itecard_set_baudrate_nolock(struct itecard_handle *const handle, const uint16_t baudrate)
{
	struct ite_devctl_data d;

	d.code = ITE_DEVCTL_UART_SET_BAUDRATE;
	d.uart_baudrate = baudrate;

	if (ite_devctl_nolock(&handle->ite, ITE_IOCTL_IN, &d) == false) {
		internal_err(L"_itecard_set_baudrate_nolock: ite_devctl_nolock failed");
		return ITECARD_E_FAILED;
	}

	return ITECARD_S_OK;
}

static itecard_status_t _itecard_send_nolock(struct itecard_handle *const handle, const uint8_t *const sendBuf, const uint8_t sendLen)
{
	struct ite_devctl_data d;

	d.code = ITE_DEVCTL_UART_SEND_DATA;
	memcpy(d.uart_data.buffer, sendBuf, sendLen);
	d.uart_data.length = sendLen;

	if (ite_devctl_nolock(&handle->ite, ITE_IOCTL_OUT, &d) == false) {
		internal_err(L"_itecard_send_nolock: ite_devctl_nolock failed");
		return ITECARD_E_FAILED;
	}

	return ITECARD_S_OK;
}

static itecard_status_t _itecard_recv_nolock(struct itecard_handle *const handle, uint8_t *const recvBuf, uint8_t *const recvLen)
{
	struct ite_devctl_data d;

	d.code = ITE_DEVCTL_UART_RECV_DATA;
	d.uart_data.length = *recvLen;

	if (ite_devctl_nolock(&handle->ite, ITE_IOCTL_IN, &d) == false) {
		internal_err(L"_itecard_recv_nolock: ite_devctl_nolock failed");
		return ITECARD_E_FAILED;
	}
	else if (d.uart_data.length == 0) {
		return ITECARD_E_NO_DATA;
	}

	memcpy(recvBuf, d.uart_data.buffer, d.uart_data.length);
	*recvLen = d.uart_data.length;

	return ITECARD_S_OK;
}

static itecard_status_t _itecard_get_atr_nolock(struct itecard_handle *const handle)
{
	struct card_info *card = &handle->dev->card;

	if (card->atr_len != 0)
		return ITECARD_S_FALSE;

	uint8_t atr[64];
	uint8_t atr_len = 0;
	uint16_t wait_count = 0;

	while (wait_count < 380/*400*/)
	{
		uint8_t rl = 32 - atr_len;

		if (_itecard_recv_nolock(handle, atr + atr_len, &rl) == ITECARD_S_OK) {
			wait_count = 0;
			atr_len += rl;
		}
		else {
			wait_count++;
		}

		microSleep(card->etu * 24);
	}

	memcpy(card->atr, atr, atr_len);
	card->atr_len = atr_len;

	return ITECARD_S_OK;
}

static itecard_status_t _itecard_transmit_t1_nolock(struct itecard_handle *const handle, const uint8_t *const sendBuf, const uint32_t sendLen, uint8_t *const recvBuf, uint32_t *const recvLen)
{
	itecard_status_t ret;
	struct card_info *card = &handle->dev->card;

	{
		uint8_t pos = 0;

		while (pos < sendLen)
		{
			uint8_t send_len = 0;

			send_len = ((sendLen - pos) > 255) ? 255 : sendLen - pos;

			ret = _itecard_send_nolock(handle, sendBuf + pos, send_len);
			if (ret != ITECARD_S_OK) {
				internal_err(L"_itecard_transmit_t1_nolock: _itecard_send_nolock failed");
				return ret;
			}
			pos += send_len;
		}
	}

	Sleep(card->T1.BGT / 1000 + 1);

	uint32_t wt;	// time limit (in milliseconds)
	uint32_t st;
	uint32_t ct;
	uint32_t cl;

	wt = (card->T1.BWT - card->T1.BGT) / 1000 + 1;
	st = (card->etu * 32) / 1000 + 1;
	ct = 0;
	cl = 0;

	while (ct <= wt)
	{
		uint8_t rl = ((*recvLen - cl) > 255) ? 255 : ((*recvLen - cl));

		ret = _itecard_recv_nolock(handle, recvBuf + cl, &rl);
		if (ret == ITECARD_S_OK) {
			if (cl == 0) {
				wt = card->T1.CWT / 1000 + 1;
			}
			cl += rl;
			ct = 0;
		}
		else {
			ct += st;
		}

		Sleep(st);
	}

	*recvLen = cl;

	return (cl == 0) ? ITECARD_E_NO_DATA : ITECARD_S_OK;
}

static itecard_status_t _itecard_transmit_t1_data_nolock(struct itecard_handle *const handle, const uint8_t code, const uint8_t *const req, const uint32_t req_len, uint8_t *const res, uint32_t *const res_len)
{
	struct card_info *card = &handle->dev->card;

	if (req_len > card->T1.IFSC)
		return ITECARD_E_TOO_LARGE;

	uint8_t block[259];
	uint8_t block2[259];
	bool is_iblock = false;

	{
		int r;

		r = card_T1MakeBlock(card, block, code, req, req_len & 0xff);
		if (r == -1) {
			internal_err(L"_itecard_transmit_t1_data_nolock: card_T1MakeBlock failed");
			return ITECARD_E_INTERNAL;
		}

		is_iblock = (r == 0) ? true : false;
	}

	itecard_status_t r = ITECARD_E_INTERNAL;
	int retry_count = 0;
	uint8_t *p;
	p = block;

	while (retry_count < 4)
	{
		itecard_status_t ret;

		if (card->T1.IFSD < 254 && is_iblock == true)
		{
			// Update IFSD

			uint8_t ifsd = 254;
			uint8_t res[254];
			uint32_t res_len = 254;

			ret = _itecard_transmit_t1_data_nolock(handle, 0xC1, &ifsd, sizeof(ifsd), res, &res_len);
			if (ret != ITECARD_S_OK) {
				dbg(L"_itecard_transmit_t1_data_nolock: IFS request failed");
				r = ret;
				break;
			}
			else if (res[0] != 254) {
				dbg(L"_itecard_transmit_t1_data_nolock: IFSD != 254");
				r = ITECARD_E_FAILED;
				break;
			}

			card->T1.IFSD = 254;
		}

		uint8_t recv_block[259];
		uint32_t recv_len = 259;

		ret = _itecard_transmit_t1_nolock(handle, p, 4 + p[2], recv_block, &recv_len);
		if (ret == ITECARD_S_OK)
		{
			bool edc, rblock = false;

			edc = card_T1CheckBlockEDC(card, recv_block, recv_len);
			if (edc == true && (4 + recv_block[2]) == recv_len)
			{
				// correct block

				if ((recv_block[1] & 0xC0) != 0x80)
				{
					// I-Block or S-Block

					if (*res_len < recv_block[2]) {
						r = ITECARD_E_INSUFFICIENT_BUFFER;
					}
					else {
						memcpy(res, recv_block + 3, recv_block[2]);
						*res_len = recv_block[2];
						if (is_iblock == true) {
							card->T1.seq ^= 1;
						}
						r = ITECARD_S_OK;
					}

					retry_count = 0;
					break;
				}
				else {
					// R-Block from card
					dbg(L"_itecard_transmit_t1_data_nolock: R-Block");
					rblock = true;
				}
			}
			else {
				internal_err(L"_itecard_transmit_t1_data_nolock: block error");
			}

			retry_count++;

			if (retry_count > 3) {
				internal_err(L"_itecard_transmit_t1_data_nolock: retry_count >= 3");
				r = ITECARD_E_COMM_FAILED;
				break;
			}
			else if (retry_count == 3 && code != 0xC0)
			{
				// RESYNCH Request

				uint8_t res[254];
				uint32_t res_len = 254;

				ret = _itecard_transmit_t1_data_nolock(handle, 0xC0, NULL, 0, res, &res_len);
				if (ret != ITECARD_S_OK) {
					dbg(L"_itecard_transmit_t1_data_nolock: RESYNCH request failed");
					r = ret;
					break;
				}

				card->T1.seq = 0;
				card->T1.IFSD = 32;

				if (is_iblock == true) {
					block[1] = 0x00;
				}
				p = block;
			}
			else if (is_iblock == true && (rblock == false || ((recv_block[1] & 0x10) >> 4) != card->T1.seq))
			{
				// R-Block

				card_T1MakeBlock(card, block2, 0x80 | ((edc == false) ? 0x01 : 0x02), NULL, 0);
				p = block2;
			}

			continue;
		}
		else if (ret == ITECARD_E_NO_DATA) {
			internal_err(L"_itecard_transmit_t1_data_nolock: no data");
			r = ITECARD_E_COMM_FAILED;
			break;
		}
		else {
			internal_err(L"_itecard_transmit_t1_data_nolock: _itecard_transmit_t1_nolock failed");
			r = ret;
			break;
		}
	}

	return r;
}

static itecard_status_t _itecard_init_nolock(struct itecard_handle *const handle)
{
	itecard_status_t r = ITECARD_E_INTERNAL, ret;
	bool b = false;

	// detect
	ret = _itecard_detect_nolock(handle, &b);
	if (ret != ITECARD_S_OK) {
		internal_err(L"_itecard_init_nolock: _itecard_detect_nolock failed");
		return ret;
	}

	if (b == false) {
		internal_err(L"_itecard_init_nolock: card not found");
		return ITECARD_E_NO_CARD;
	}

	if (handle->dev->card.atr_len != 0) {
		return ITECARD_S_FALSE;
	}

	// reset
	ret = _itecard_reset_nolock(handle);
	if (ret != ITECARD_S_OK) {
		internal_err(L"_itecard_init_nolock: _itecard_reset_nolock failed");
		return ret;
	}

	Sleep(10);

	struct card_info *card = &handle->dev->card;

	card_init(card);

	// get atr
	ret = _itecard_get_atr_nolock(handle);
	if (ret != ITECARD_S_OK) {
		internal_err(L"_itecard_init_nolock: _itecard_get_atr_nolock failed");
		return ret;
	}

	if (card->atr_len == 0) {
		internal_err(L"_itecard_init_nolock: unresponsive card");
		card_clear(card);
		return ITECARD_E_UNRESPONSIVE_CARD;
	}

	// parse atr
	if (card_parseATR(card) == false) {
		internal_err(L"_itecard_init_nolock: unsupported card");
		card_clear(card);
		return ITECARD_E_UNSUPPORTED_CARD;
	}

	// set baudrate
	ret = _itecard_set_baudrate_nolock(handle, 19200);
	if (ret != ITECARD_S_OK) {
		internal_err(L"_itecard_init_nolock: _itecard_set_baudrate_nolock failed");
		card_clear(card);
		return ret;
	}

	return ITECARD_S_OK;
}

itecard_status_t itecard_detect(struct itecard_handle *const handle, bool *const b)
{
	itecard_status_t ret;

	_itecard_db_lock(&handle->db);
	ite_lock(&handle->ite);

	ret = _itecard_detect_nolock(handle, b);

	ite_unlock(&handle->ite);
	_itecard_db_unlock(&handle->db);

	return ret;
}

// init card
itecard_status_t itecard_init(struct itecard_handle *const handle)
{
	itecard_status_t ret;

	_itecard_db_lock(&handle->db);
	ite_lock(&handle->ite);

	ret = _itecard_init_nolock(handle);

	ite_unlock(&handle->ite);
	_itecard_db_unlock(&handle->db);

	return ret;
}

itecard_status_t itecard_transmit(struct itecard_handle *const handle, const itecard_protocol_t protocol, const uint8_t *const sendBuf, const uint32_t sendLen, uint8_t *const recvBuf, uint32_t *const recvLen)
{
	_itecard_db_lock(&handle->db);
	ite_lock(&handle->ite);

	itecard_status_t r = ITECARD_E_INTERNAL, ret;

	ret = _itecard_init_nolock(handle);
	if (ret != ITECARD_S_OK && ret != ITECARD_S_FALSE) {
		internal_err(L"itecard_transmit: _itecard_init_nolock failed");
		return ret;
	}

	switch (protocol)
	{
	case ITECARD_PROTOCOL_T1:
		ret = _itecard_transmit_t1_data_nolock(handle, 0x00, sendBuf, sendLen, recvBuf, recvLen);
		if (ret != ITECARD_S_OK) {
			internal_err(L"itecard_transmit: _itecard_transmit_t1_data_nolock failed (%08X)", ret);
		}
		break;

	default:
		ret = ITECARD_E_UNSUPPORTED;
	}

	dbg(L"itecard_transmit: ret: %d", ret);

	ite_unlock(&handle->ite);
	_itecard_db_unlock(&handle->db);

	return ret;
}