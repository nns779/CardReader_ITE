// winscard.c

#include <stdbool.h>
#include <stdint.h>
#include <windows.h>

#include "debug.h"
#include "memory.h"
#include "string.h"
#include "handle.h"
#include "devdb.h"
#include "itecard.h"

/* macros */

#define _CONTEXT_BASE	0x8c110000
#define _HANDLE_BASE	0xda910000

#define _CONTEXT_SIGNATURE	0x83fc937b
#define _HANDLE_SIGNATURE	0xa7350c12

#define _context_check_signature(ctx) ((ctx)->signature == _CONTEXT_SIGNATURE)
#define _context_check(ctx) ((ctx) != NULL && _context_check_signature((ctx)))

#define _handle_check_signature(handle) ((handle)->signature == _HANDLE_SIGNATURE)
#define _handle_check(handle) ((handle) != NULL && _handle_check_signature((handle)))

#define _READER_NAME_SIZE_A(dev) ((dev)->reader_len_A + 1 + 10 + 1 + 1)
#define _READER_NAME_SIZE_W(dev) ((dev)->reader_len_W + 1 + 10 + 1 + 1)

/* structures */

struct _context {
	uint32_t signature;
	CRITICAL_SECTION sct;
};

struct _handle {
	uint32_t signature;
	CRITICAL_SECTION sct;
	uint32_t id;
	struct _reader_device *dev;
	struct itecard_handle itecard;
};

struct _reader_device {
	devdb db;
	wchar_t reader_W[128];
	uint32_t reader_len_W;
	char reader_A[128];
	uint32_t reader_len_A;
};

struct _reader_list_A
{
	struct _reader_device *dev;
	uint32_t size;
	uint32_t len;
	char *list;
};

struct _reader_list_W
{
	struct _reader_device *dev;
	uint32_t size;
	uint32_t len;
	wchar_t *list;
};

/* variables */

const SCARD_IO_REQUEST __g_rgSCardT1Pci = { SCARD_PROTOCOL_T1, sizeof(SCARD_IO_REQUEST) };

static HANDLE _hEvent = NULL;

static handle_list _hlist_ctx;
static handle_list _hlist_card;

static uint32_t _reader_all_len_W = 0;
static uint32_t _reader_all_len_A = 0;

static struct _reader_device *_device = NULL;
static uintptr_t _device_num = 0;

/* functions */

static LONG itecard_status_to_scard_status(itecard_status_t status)
{
	switch (status)
	{
	case ITECARD_S_OK:
	case ITECARD_S_FALSE:
		return SCARD_S_SUCCESS;

	case ITECARD_E_FAILED:
		return SCARD_F_COMM_ERROR;

	case ITECARD_E_FATAL:
		return SCARD_F_UNKNOWN_ERROR;

	case ITECARD_E_INTERNAL:
	case ITECARD_E_INVALID_PARAMETER:
		return SCARD_F_INTERNAL_ERROR;

	case ITECARD_E_UNSUPPORTED:
		return SCARD_E_READER_UNSUPPORTED;

	case ITECARD_E_TOO_LARGE:
		return SCARD_E_INVALID_PARAMETER;

	case ITECARD_E_INSUFFICIENT_BUFFER:
		return SCARD_E_INSUFFICIENT_BUFFER;

	case ITECARD_E_NO_MEMORY:
		return SCARD_E_NO_MEMORY;

	case ITECARD_E_NO_DEVICE:
		return SCARD_E_READER_UNAVAILABLE;

	case ITECARD_E_NOT_READY:
		return SCARD_E_NOT_READY;

	case ITECARD_E_NO_CARD:
		return SCARD_W_REMOVED_CARD;

	case ITECARD_E_NOT_SHARED:
	case ITECARD_E_SHARED:
		return SCARD_E_SHARING_VIOLATION;

	case ITECARD_E_UNRESPONSIVE_CARD:
		return SCARD_W_UNRESPONSIVE_CARD;

	case ITECARD_E_UNSUPPORTED_CARD:
		return SCARD_W_UNSUPPORTED_CARD;

	case ITECARD_E_PROTO_MISMATCH:
		return SCARD_E_PROTO_MISMATCH;

	case ITECARD_E_COMM_FAILED:
		return SCARD_E_COMM_DATA_LOST;

	default:
		dbg("itecard_status_to_scard_status: %d", status);
		return SCARD_F_INTERNAL_ERROR;
	}
}

static LONG devdb_status_to_scard_status(devdb_status_t status)
{
	switch (status)
	{
	case DEVDB_S_OK:
		return SCARD_S_SUCCESS;

	case DEVDB_E_INTERNAL:
		return SCARD_F_INTERNAL_ERROR;

	case DEVDB_E_NO_MEMORY:
		return SCARD_E_NO_MEMORY;

	case DEVDB_E_API:
		return SCARD_F_UNKNOWN_ERROR;

	case DEVDB_E_NO_DEVICES:
		return SCARD_E_NO_READERS_AVAILABLE;

	case DEVDB_E_DEVICE_NOT_FOUND:
		return SCARD_E_READER_UNAVAILABLE;

	default:
		return SCARD_F_INTERNAL_ERROR;
	}
}

static int _enum_readers_callback_A(devdb *const db, const uint32_t id, const struct devdb_shared_devinfo *const devinfo, void *prm)
{
	struct _reader_list_A *rl = prm;
	struct _reader_device *rd = rl->dev;

	char name[256];
	uint32_t id_len, name_len;

	memcpy(name, rd->reader_A, rd->reader_len_A * sizeof(char));
	name[rd->reader_len_A] = ' ';
	id_len = strFromUInt32(name + rd->reader_len_A + 1, 11, id, 10);

	name_len = rd->reader_len_A + 1 + id_len + 1;

	if (rl->list != NULL)
	{
		if (rl->len + name_len > rl->size) {
			rl->len = 0;
			return 0;
		}
		memcpy(rl->list + rl->len, name, name_len * sizeof(char));
	}

	rl->len += name_len;

	return 1;
}

static int _enum_readers_callback_W(devdb *const db, const uint32_t id, const struct devdb_shared_devinfo *const devinfo, void *prm)
{
	struct _reader_list_W *rl = prm;
	struct _reader_device *rd = rl->dev;

	wchar_t name[256];
	uint32_t id_len, name_len;

	memcpy(name, rd->reader_W, rd->reader_len_W * sizeof(wchar_t));
	name[rd->reader_len_W] = L' ';
	id_len = wstrFromUInt32(name + rd->reader_len_W + 1, 11, id, 10);

	name_len = rd->reader_len_W + 1 + id_len + 1;

	if (rl->list != NULL)
	{
		if (rl->len + name_len > rl->size) {
			rl->len = 0;
			return 0;
		}
		memcpy(rl->list + rl->len, name, name_len * sizeof(wchar_t));
	}

	rl->len += name_len;

	return 1;
}

static bool _get_reader_id_A(const char *const name, uintptr_t *const pos, struct _reader_device **const rd, uint32_t *const id)
{
	struct _reader_device *d = _device;
	uintptr_t n = _device_num;

	if (*pos >= n) {
		return false;
	}

	d += *pos;
	n -= *pos;

	while (n--) {
		if (strCompareN(d->reader_A, name, d->reader_len_A) != false && name[d->reader_len_A] == ' ' && strToUInt32(&name[d->reader_len_A + 1], id) != false) {
			*rd = d;
			*pos = d - _device;
			return true;
		}
		d++;
	}

	return false;
}

static bool _get_reader_id_W(const wchar_t *const name, uintptr_t *const pos, struct _reader_device **const rd, uint32_t *const id)
{
	struct _reader_device *d = _device;
	uintptr_t n = _device_num;

	if (*pos >= n) {
		return false;
	}

	d += *pos;
	n -= *pos;

	while (n--) {
		if (wstrCompareN(d->reader_W, name, d->reader_len_W) != false && name[d->reader_len_W] == L' ' && wstrToUInt32(&name[d->reader_len_W + 1], id) != false) {
			*rd = d;
			*pos = d - _device;
			return true;
		}
		d++;
	}

	return false;
}

static LONG _connect_card(struct _handle *const handle, struct _reader_device *const rd, const uint32_t id, DWORD dwShareMode, DWORD dwPreferredProtocols, LPDWORD pdwActiveProtocol)
{
	bool exclusive;

	if (dwShareMode == SCARD_SHARE_DIRECT)
		return SCARD_E_READER_UNSUPPORTED;

	exclusive = (dwShareMode == SCARD_SHARE_EXCLUSIVE) ? true : false;

	itecard_protocol_t protocol = ITECARD_PROTOCOL_UNDEFINED;

	if (dwPreferredProtocols & SCARD_PROTOCOL_T1)
		protocol |= ITECARD_PROTOCOL_T1;

	if (protocol == ITECARD_PROTOCOL_UNDEFINED)
		return SCARD_E_READER_UNSUPPORTED;

	LONG r = SCARD_F_INTERNAL_ERROR;
	devdb_status_t dbr;
	struct devdb_shared_devinfo *devinfo;
	struct itecard_shared_readerinfo *reader;

	dbr = devdb_get_shared_devinfo_nolock(&rd->db, id, &devinfo);
	if (dbr != DEVDB_S_OK) {
		internal_err("_connect_card: devdb_get_shared_devinfo_nolock failed");
		r = devdb_status_to_scard_status(dbr);
		goto end1;
	}

	reader = (struct itecard_shared_readerinfo *)devinfo->user;

	if (exclusive == true && (devinfo->ref > 0)) {
		internal_err("_connect_card: device was opened in share mode by another application");
		r = SCARD_E_SHARING_VIOLATION;
		goto end1;
	}

	itecard_status_t cr;

	cr = itecard_open(&handle->itecard, devinfo->path, reader, protocol, exclusive);
	if (cr != ITECARD_S_OK) {
		internal_err("_connect_card: itecard_open failed");
		r = itecard_status_to_scard_status(cr);
		goto end1;
	}

	cr = itecard_init(&handle->itecard);
	if (cr != ITECARD_S_OK && cr != ITECARD_S_FALSE) {
		internal_err("_connect_card: itecard_init failed");
		r = itecard_status_to_scard_status(cr);
		goto end2;
	}

	if ((dwPreferredProtocols & SCARD_PROTOCOL_T1) && (reader->card.T1.b == true))
		*pdwActiveProtocol |= SCARD_PROTOCOL_T1;

	if (*pdwActiveProtocol == SCARD_PROTOCOL_UNDEFINED) {
		internal_err("_connect_card: no active protocol");
		r = SCARD_E_PROTO_MISMATCH;
		goto end2;
	}

	if (devdb_ref_nolock(&rd->db, id, NULL) != DEVDB_S_OK) {
		internal_err("_connect_card: devdb_ref_nolock failed");
		r = SCARD_F_INTERNAL_ERROR;
		goto end2;
	}

	handle->id = id;
	handle->dev = rd;

	return SCARD_S_SUCCESS;

end2:
	itecard_close(&handle->itecard, true, (devinfo->ref == 0) ? true : false);
end1:
	return r;
}

static LONG _disconnect_card(struct _handle *const handle, const bool reset)
{
	uint32_t ref;
	LONG r;

	if (devdb_unref_nolock(&handle->dev->db, handle->id, &ref) == DEVDB_S_OK) {
		r = itecard_status_to_scard_status(itecard_close(&handle->itecard, reset, (ref == 0) ? true : false));
	}
	else {
		r = SCARD_F_INTERNAL_ERROR;
	}

	return r;
}

static LONG _get_card_atr(struct itecard_handle *const itecard, LPBYTE pbAtr, LPDWORD pcbAtrLen, uint32_t max_atr_len)
{
	LONG r = SCARD_S_SUCCESS;

	if (pcbAtrLen != NULL)
	{
		struct card_info *card = &itecard->reader->card;
		uint8_t atr_len;

		atr_len = (card->atr_len > max_atr_len) ? max_atr_len : card->atr_len;

		if (pbAtr != NULL)
		{
			uint8_t *atr = NULL;

			if (*pcbAtrLen == SCARD_AUTOALLOCATE)
			{
				atr = memAlloc(atr_len);
				if (atr == NULL) {
					r = SCARD_E_NO_MEMORY;
					goto end;
				}

				*((LPBYTE *)pbAtr) = atr;
			}
			else {
				if (*pcbAtrLen < atr_len) {
					r = SCARD_E_INSUFFICIENT_BUFFER;
				}
				else {
					atr = pbAtr;
				}
			}

			if (atr != NULL) {
				memcpy(atr, card->atr, atr_len);
			}
		}

		*pcbAtrLen = atr_len;
	}

end:
	return r;
}

static DWORD _get_reader_state(struct _reader_device *const rd, const uint32_t id, LPDWORD pcbAtr, LPBYTE rgbAtr)
{
	DWORD state = 0;
	struct devdb_shared_devinfo *devinfo;

	if (devdb_get_shared_devinfo_nolock(&rd->db, id, &devinfo) != DEVDB_S_OK) {
		state = SCARD_STATE_UNAVAILABLE;
	}
	else
	{
		struct itecard_shared_readerinfo *reader;
		struct itecard_handle h;

		reader = (struct itecard_shared_readerinfo *)devinfo->user;

		if (itecard_open(&h, devinfo->path, reader, ITECARD_PROTOCOL_UNDEFINED, false) != ITECARD_S_OK) {
			state = SCARD_STATE_UNAVAILABLE;
		}
		else {
			switch (itecard_init(&h))
			{
			case ITECARD_S_OK:
			case ITECARD_S_FALSE:
				state = SCARD_STATE_PRESENT;
				break;

			case ITECARD_E_NO_CARD:
			case ITECARD_E_FAILED:
				state = SCARD_STATE_EMPTY;
				break;

			default:
				state = SCARD_STATE_MUTE;
				break;
			}

			if (state == SCARD_STATE_PRESENT)
			{
				if (devinfo->ref > 0) {
					state |= (reader->exclusive == true) ? SCARD_STATE_EXCLUSIVE : SCARD_STATE_INUSE;
				}

				_get_card_atr(&h, rgbAtr, pcbAtr, 36);
			}

			itecard_close(&h, false, false);
		}
	}

	return state;
}

static LONG _get_card_status(struct _handle *const handle, LPDWORD pdwState, LPDWORD pdwProtocol)
{
	struct itecard_handle *itecard = &handle->itecard;
	itecard_status_t ret;

	ret = itecard_init(itecard);
	if (ret == ITECARD_E_FAILED) {
		ret = ITECARD_E_NO_CARD;
	}

	if (pdwState != NULL)
	{
		if (ret == ITECARD_E_NO_CARD) {
			*pdwState = SCARD_ABSENT;
		}
		else if (ret == ITECARD_S_OK || ret == ITECARD_S_FALSE)
		{
			DWORD protocol = SCARD_PROTOCOL_UNDEFINED;

			if ((itecard->protocol & ITECARD_PROTOCOL_T1) && (itecard->reader->card.T1.b == true))
				protocol |= SCARD_PROTOCOL_T1;

			if (protocol == SCARD_PROTOCOL_UNDEFINED) {
				*pdwState = SCARD_POWERED;
			}
			else {
				*pdwState = SCARD_SPECIFIC;

				if (pdwProtocol != NULL) {
					*pdwProtocol = protocol;
				}
			}
		}
		else {
			*pdwState = SCARD_POWERED;
		}
	}

	return SCARD_S_SUCCESS;
}

static bool _context_alloc(struct _context **const ctx)
{
	struct _context *c;

	c = memAlloc(sizeof(struct _context));
	if (c == NULL)
		return false;

	c->signature = _CONTEXT_SIGNATURE;
	InitializeCriticalSection(&c->sct);

	*ctx = c;

	return true;
}

static bool _context_free(struct _context *const ctx)
{
	DeleteCriticalSection(&ctx->sct);
	memFree(ctx);

	return true;
}

static uintptr_t _context_release_callback(void *h, void *prm)
{
	_context_free(h);
	return 0;
}

static bool _handle_alloc(struct _handle **const handle)
{
	struct _handle *h;

	h = memAlloc(sizeof(struct _handle));
	if (h == NULL)
		return false;

	h->signature = _HANDLE_SIGNATURE;
	InitializeCriticalSection(&h->sct);

	*handle = h;

	return true;
}

static bool _handle_free(struct _handle *const handle)
{
	DeleteCriticalSection(&handle->sct);
	memFree(handle);

	return true;
}

static uintptr_t _handle_release_callback(void *h, void *prm)
{
	LONG r;
	struct _handle *handle = (struct _handle *)h;

	devdb_lock(&handle->dev->db);
	r = _disconnect_card(handle, *((bool *)prm));
	devdb_unlock(&handle->dev->db);

	_handle_free(h);

	return (uintptr_t)r;
}

static bool _reader_device_load(const uint8_t dev_id, struct _reader_device *const rd, const wchar_t *const path)
{
	wchar_t name[32], def[32];

	memcpy(name, L"ReaderDevice", 12 * sizeof(wchar_t));
	wstrFromUInt32(name + 12, 11, dev_id, 10);

	memcpy(def, L"ITE ICC Reader ", 15 * sizeof(wchar_t));
	wstrFromUInt32(def + 15, 11, dev_id, 10);

	wchar_t friendlyName[128];

	if (GetPrivateProfileStringW(name, L"FriendlyName", NULL, friendlyName, 128, path) == 0) {
		dbg("_reader_device_load: GetPrivateProfileStringW(FriendlyName): empty");
		return false;
	}

	rd->reader_len_W = GetPrivateProfileStringW(name, L"ReaderName", def, rd->reader_W, 128, path);
	rd->reader_len_A = WideCharToMultiByte(CP_ACP, 0, rd->reader_W, -1, rd->reader_A, 128, NULL, NULL);
	if (rd->reader_len_A == 0) {
		dbg("_reader_device_load: rd->reader_len_A == 0");
		return false;
	}
	rd->reader_len_A--;

	wchar_t uniqueID[DEVDB_MAX_ID_SIZE];

	GetPrivateProfileStringW(name, L"UniqueID", L"", uniqueID, DEVDB_MAX_ID_SIZE, path);

	if (devdb_open(&rd->db, friendlyName, uniqueID, sizeof(struct itecard_shared_readerinfo)) != DEVDB_S_OK) {
		dbg("_reader_device_load: devdb_open() failed");
		return false;
	}

	_reader_all_len_W += (rd->reader_len_W + 1 + 10 + 1) * DEVDB_MAX_DEV_NUM;
	_reader_all_len_A += (rd->reader_len_A + 1 + 10 + 1) * DEVDB_MAX_DEV_NUM;

	return true;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
	switch (fdwReason)
	{
	case DLL_PROCESS_ATTACH:
	{
		if (memInit() == false) {
			return FALSE;
		}

		wchar_t path[MAX_PATH + 1];
		DWORD ret;

		ret = GetModuleFileNameW(hinstDLL, path, MAX_PATH);
		if (ret == 0 || wstrCompare(path + ret - 4, L".dll") == false) {
			return FALSE;
		}

		memcpy(path + ret - 3, L"ini", 3 * sizeof(wchar_t));

		if (GetPrivateProfileIntW(L"Debug", L"Enable", 0, path) != 0)
		{
			dbg_enable(true);

			if (GetPrivateProfileIntW(L"Debug", L"OutputToFile", 0, path) != 0)
			{
				wchar_t path2[MAX_PATH + 1];

				memcpy(path2, path, ret);
				memcpy(path + ret - 3, L"log", 3 * sizeof(wchar_t));
				path2[ret] = L'\0';
				dbg_open(path);
			}
		}

		uintptr_t max_ctx, max_card;
		wchar_t use_dev[1024];
		uint32_t use_dev_len;
		uint8_t dev_ids[256];

		max_ctx = GetPrivateProfileIntW(L"ResourceManager", L"MaxContextNum", 32, path);
		max_card = GetPrivateProfileIntW(L"CardReader", L"MaxHandleNum", 32, path);
		use_dev_len = GetPrivateProfileStringW(L"CardReader", L"UseDevice", NULL, use_dev, 1024, path);

		{
			// UseDevice から番号を取り出す

			uint32_t i, j;

			for (i = 0, j = 0; i < use_dev_len && j < 256; i++)
			{
				uint8_t num = 0;

				while (use_dev[i] >= L'0' && use_dev[i] <= L'9') {
					num *= 10;
					num += use_dev[i] - L'0';
					i++;
				}

				if (num != 0) {
					dev_ids[j++] = num;
				}
			}

			_device_num = j;
		}

		if (_device_num == 0) {
			// UseDevice に番号が記述されていない
			goto attach_skip1;
		}

		_device = memAlloc(_device_num * sizeof(struct _reader_device));
		if (_device == NULL) {
			goto attach_err1;
		}

		_reader_all_len_W = 0;
		_reader_all_len_A = 0;

		{
			// 設定ファイルから該当する番号のデバイス情報を読み込む

			uintptr_t i, j;

			for (i = 0, j = 0; i < _device_num; i++) {
				if (_reader_device_load(dev_ids[i], &_device[j], path) != false) {
					j++;
				}
			}

			_device_num = j;
		}

	attach_skip1:
		_reader_all_len_W++;
		_reader_all_len_A++;

		if (handle_list_init(&_hlist_ctx, _CONTEXT_BASE, max_ctx, _context_release_callback) != false) {
			if (handle_list_init(&_hlist_card, _HANDLE_BASE, max_card, _handle_release_callback) != false) {
				// 初期化完了
				break;
			}
			handle_list_deinit(_hlist_ctx);
		}

		memFree(_device);

	attach_err1:
		dbg_close();
		memDeinit();

		return FALSE;
	}

	case DLL_PROCESS_DETACH:
	{
		handle_list_deinit(_hlist_card);
		handle_list_deinit(_hlist_ctx);
		{
			uintptr_t i;

			for (i = 0; i < _device_num; i++) {
				devdb_close(&_device[i].db);
			}
		}
		memFree(_device);
		dbg_close();
		memDeinit();
		break;
	}

	default:
		break;
	}

	return TRUE;
}

/* Resource Manager Context Functions */

LONG WINAPI SCardEstablishContext(DWORD dwScope, LPCVOID pvReserved1, LPCVOID pvReserved2, LPSCARDCONTEXT phContext)
{
	dbg("SCardEstablishContext(ITE)");

	if (phContext == NULL)
		return SCARD_E_INVALID_PARAMETER;

	*phContext = 0;

	struct _context *ctx;
	uintptr_t v;

	if (_context_alloc(&ctx) == false)
		return SCARD_E_NO_MEMORY;

	if (handle_list_put(_hlist_ctx, ctx, &v) == false) {
		_context_free(ctx);
		return SCARD_E_NO_MEMORY;
	}

	*phContext = (SCARDCONTEXT)v;

	return SCARD_S_SUCCESS;
}

LONG WINAPI SCardReleaseContext(SCARDCONTEXT hContext)
{
	dbg("SCardReleaseContext(ITE)");

	struct _context *ctx;
	LONG r;

	handle_list_lock(_hlist_ctx);

	handle_list_get_nolock(_hlist_ctx, hContext, &ctx);
	if (!_context_check(ctx)) {
		r = SCARD_E_INVALID_HANDLE;
		goto end;
	}

	EnterCriticalSection(&ctx->sct);
	LeaveCriticalSection(&ctx->sct);

	handle_list_release_nolock(_hlist_ctx, hContext, true, NULL, NULL);

	r = SCARD_S_SUCCESS;

end:
	handle_list_unlock(_hlist_ctx);
	return r;
}

/* Resource Manager Support Function */

LONG WINAPI SCardFreeMemory(SCARDCONTEXT hContext, LPCVOID pvMem)
{
	dbg("SCardFreeMemory(ITE)");

	if (pvMem == NULL)
		return SCARD_E_INVALID_PARAMETER;

	struct _context *ctx = NULL;

	if (hContext != 0)
	{
		handle_list_lock(_hlist_ctx);

		handle_list_get_nolock(_hlist_ctx, hContext, &ctx);
		if (!_context_check(ctx)) {
			handle_list_unlock(_hlist_ctx);
			return SCARD_E_INVALID_HANDLE;
		}

		EnterCriticalSection(&ctx->sct);
		handle_list_unlock(_hlist_ctx);

		memFree(pvMem);

		LeaveCriticalSection(&ctx->sct);
	}
	else {
		memFree(pvMem);
	}

	return SCARD_S_SUCCESS;
}

/* Smart Card Database Query Functions */

LONG WINAPI SCardListReadersA(SCARDCONTEXT hContext, LPCSTR mszGroups, LPSTR mszReaders, LPDWORD pcchReaders)
{
	dbg("SCardListReadersA(ITE)");

	if (pcchReaders == NULL)
		return SCARD_E_INVALID_PARAMETER;

	struct _context *ctx = NULL;

	if (hContext != 0)
	{
		handle_list_lock(_hlist_ctx);

		handle_list_get_nolock(_hlist_ctx, hContext, &ctx);
		if (!_context_check(ctx)) {
			handle_list_unlock(_hlist_ctx);
			return SCARD_E_INVALID_HANDLE;
		}

		EnterCriticalSection(&ctx->sct);
		handle_list_unlock(_hlist_ctx);
	}

	LONG r = SCARD_F_INTERNAL_ERROR;

	bool auto_alloc = false;
	struct _reader_list_A rl;

	rl.size = 0;
	rl.len = 0;
	rl.list = NULL;

	if (mszReaders != NULL)
	{
		if (*pcchReaders == SCARD_AUTOALLOCATE)
		{
			auto_alloc = true;

			rl.list = memAlloc(_reader_all_len_A * sizeof(char));
			if (rl.list == NULL) {
				internal_err("SCardListReadersA(ITE): memAlloc failed");
				r = SCARD_E_NO_MEMORY;
				goto end;
			}

			rl.size = _reader_all_len_A - 1;

			*((LPSTR *)mszReaders) = rl.list;
		}
		else {
			rl.list = mszReaders;
			rl.size = *pcchReaders - 1;
		}
	}

	devdb_status_t ret = DEVDB_E_INTERNAL;
	uintptr_t i;

	for (i = 0; i < _device_num; i++)
	{
		struct _reader_device *dev = &_device[i];

		devdb_lock(&dev->db);

		ret = devdb_update_nolock(&dev->db);
		if (ret != DEVDB_S_OK && ret != DEVDB_E_NO_DEVICES) {
			r = devdb_status_to_scard_status(ret);
			devdb_unlock(&dev->db);
			break;
		}

		rl.dev = dev;

		ret = devdb_enum_nolock(&dev->db, _enum_readers_callback_A, &rl);
		if (ret != DEVDB_E_NO_DEVICES) {
			if (ret != DEVDB_S_OK) {
				r = SCARD_F_INTERNAL_ERROR;
				devdb_unlock(&dev->db);
				break;
			}
			else if (rl.len == 0) {
				r = SCARD_E_INSUFFICIENT_BUFFER;
				devdb_unlock(&dev->db);
				break;
			}
			else {
				r = SCARD_S_SUCCESS;
			}
		}
		else if (r != SCARD_S_SUCCESS) {
			r = SCARD_E_NO_READERS_AVAILABLE;
		}

		devdb_unlock(&dev->db);
	}

end:
	if (r == SCARD_S_SUCCESS)
	{
		if (rl.list != NULL) {
			rl.list[rl.len] = '\0';
		}

		*pcchReaders = rl.len + 1;
	}
	else
	{
		if (auto_alloc == true)
		{
			*((LPSTR *)mszReaders) = NULL;

			if (rl.list != NULL) {
				memFree(rl.list);
			}
		}
		*pcchReaders = 0;
	}

	if (ctx != NULL) {
		LeaveCriticalSection(&ctx->sct);
	}

	return r;
}

LONG WINAPI SCardListReadersW(SCARDCONTEXT hContext, LPCWSTR mszGroups, LPWSTR mszReaders, LPDWORD pcchReaders)
{
	dbg("SCardListReadersW(ITE)");

	if (pcchReaders == NULL)
		return SCARD_E_INVALID_PARAMETER;

	struct _context *ctx = NULL;

	if (hContext != 0)
	{
		handle_list_lock(_hlist_ctx);

		handle_list_get_nolock(_hlist_ctx, hContext, &ctx);
		if (!_context_check(ctx)) {
			handle_list_unlock(_hlist_ctx);
			return SCARD_E_INVALID_HANDLE;
		}

		EnterCriticalSection(&ctx->sct);
		handle_list_unlock(_hlist_ctx);
	}

	LONG r = SCARD_F_INTERNAL_ERROR;

	bool auto_alloc = false;
	struct _reader_list_W rl;

	rl.size = 0;
	rl.len = 0;
	rl.list = NULL;

	if (mszReaders != NULL)
	{
		if (*pcchReaders == SCARD_AUTOALLOCATE)
		{
			auto_alloc = true;

			rl.list = memAlloc(_reader_all_len_W * sizeof(wchar_t));
			if (rl.list == NULL) {
				internal_err("SCardListReadersW(ITE): memAlloc failed");
				r = SCARD_E_NO_MEMORY;
				goto end;
			}

			rl.size = _reader_all_len_W - 1;

			*((LPWSTR *)mszReaders) = rl.list;
		}
		else {
			rl.list = mszReaders;
			rl.size = *pcchReaders - 1;
		}
	}

	devdb_status_t ret = DEVDB_E_INTERNAL;
	uintptr_t i;

	for (i = 0; i < _device_num; i++)
	{
		struct _reader_device *dev = &_device[i];

		devdb_lock(&dev->db);

		ret = devdb_update_nolock(&dev->db);
		if (ret != DEVDB_S_OK && ret != DEVDB_E_NO_DEVICES) {
			r = devdb_status_to_scard_status(ret);
			devdb_unlock(&dev->db);
			break;
		}

		rl.dev = dev;

		ret = devdb_enum_nolock(&dev->db, _enum_readers_callback_W, &rl);
		if (ret != DEVDB_E_NO_DEVICES) {
			if (ret != DEVDB_S_OK) {
				r = SCARD_F_INTERNAL_ERROR;
				devdb_unlock(&dev->db);
				break;
			}
			else if (rl.len == 0) {
				r = SCARD_E_INSUFFICIENT_BUFFER;
				devdb_unlock(&dev->db);
				break;
			}
			else {
				r = SCARD_S_SUCCESS;
			}
		}
		else if (r != SCARD_S_SUCCESS) {
			r = SCARD_E_NO_READERS_AVAILABLE;
		}

		devdb_unlock(&dev->db);
	}

end:
	if (r == SCARD_S_SUCCESS)
	{
		if (rl.list != NULL) {
			rl.list[rl.len] = L'\0';
		}

		*pcchReaders = rl.len + 1;
	}
	else
	{
		if (auto_alloc == true)
		{
			*((LPWSTR *)mszReaders) = NULL;

			if (rl.list != NULL) {
				memFree(rl.list);
			}
		}
		*pcchReaders = 0;
	}

	if (ctx != NULL) {
		LeaveCriticalSection(&ctx->sct);
	}

	return r;
}

/* Smart Card Tracking Functions */

LONG WINAPI SCardGetStatusChangeA(SCARDCONTEXT hContext, DWORD dwTimeout, LPSCARD_READERSTATEA rgReaderStates, DWORD cReaders)
{
	dbg("SCardGetStatusChangeA(ITE)");

	if (rgReaderStates == NULL || cReaders == 0)
		return SCARD_E_INVALID_PARAMETER;

	if (dwTimeout != 0)
		return SCARD_E_READER_UNSUPPORTED;

	struct _context *ctx;

	handle_list_lock(_hlist_ctx);

	handle_list_get_nolock(_hlist_ctx, hContext, &ctx);
	if (!_context_check(ctx)) {
		handle_list_unlock(_hlist_ctx);
		return SCARD_E_INVALID_HANDLE;
	}

	EnterCriticalSection(&ctx->sct);
	handle_list_unlock(_hlist_ctx);

	LONG r = SCARD_S_SUCCESS;

	for (uint32_t i = 0; i < cReaders; i++)
	{
		if (strCompare(rgReaderStates[i].szReader, "\\\\?PnP?\\Notification") == true) {
			r = SCARD_E_READER_UNSUPPORTED;
			break;
		}

		rgReaderStates[i].cbAtr = 0;

		DWORD state = 0;
		uintptr_t pos = 0;
		struct _reader_device *dev;
		uint32_t id;

		while (1) {
			if (_get_reader_id_A(rgReaderStates[i].szReader, &pos, &dev, &id) == false) {
				state = SCARD_STATE_IGNORE | SCARD_STATE_UNKNOWN;
				break;
			}

			devdb_lock(&dev->db);
			state = _get_reader_state(dev, id, &rgReaderStates[i].cbAtr, rgReaderStates[i].rgbAtr);
			devdb_unlock(&dev->db);

			if (state != SCARD_STATE_UNAVAILABLE || pos >= _device_num) {
				break;
			}

			pos++;
		}

		rgReaderStates[i].dwEventState = state | ((state != rgReaderStates[i].dwCurrentState) ? SCARD_STATE_CHANGED : 0);
	}

	LeaveCriticalSection(&ctx->sct);

	return r;
}

LONG WINAPI SCardGetStatusChangeW(SCARDCONTEXT hContext, DWORD dwTimeout, LPSCARD_READERSTATEW rgReaderStates, DWORD cReaders)
{
	dbg("SCardGetStatusChangeW(ITE)");

	if (rgReaderStates == NULL || cReaders == 0)
		return SCARD_E_INVALID_PARAMETER;

	if (dwTimeout != 0)
		return SCARD_E_READER_UNSUPPORTED;

	struct _context *ctx;

	handle_list_lock(_hlist_ctx);

	handle_list_get_nolock(_hlist_ctx, hContext, &ctx);
	if (!_context_check(ctx)) {
		handle_list_unlock(_hlist_ctx);
		return SCARD_E_INVALID_HANDLE;
	}

	EnterCriticalSection(&ctx->sct);
	handle_list_unlock(_hlist_ctx);

	LONG r = SCARD_S_SUCCESS;

	for (uint32_t i = 0; i < cReaders; i++)
	{
		if (wstrCompare(rgReaderStates[i].szReader, L"\\\\?PnP?\\Notification") == true) {
			r = SCARD_E_READER_UNSUPPORTED;
			break;
		}

		rgReaderStates[i].cbAtr = 0;

		DWORD state = 0;
		uintptr_t pos = 0;
		struct _reader_device *dev;
		uint32_t id;

		while (1) {
			if (_get_reader_id_W(rgReaderStates[i].szReader, &pos, &dev, &id) == false) {
				state = SCARD_STATE_IGNORE | SCARD_STATE_UNKNOWN;
				break;
			}

			devdb_lock(&dev->db);
			state = _get_reader_state(dev, id, &rgReaderStates[i].cbAtr, rgReaderStates[i].rgbAtr);
			devdb_unlock(&dev->db);

			pos++;

			if (state != SCARD_STATE_UNAVAILABLE || pos >= _device_num) {
				break;
			}
		}

		rgReaderStates[i].dwEventState = state | ((state != rgReaderStates[i].dwCurrentState) ? SCARD_STATE_CHANGED : 0);
	}

	LeaveCriticalSection(&ctx->sct);

	return r;
}

LONG WINAPI SCardCancel(SCARDCONTEXT hContext)
{
	dbg("SCardCancel(ITE)");

	struct _context *ctx;

	handle_list_lock(_hlist_ctx);

	handle_list_get_nolock(_hlist_ctx, hContext, &ctx);
	if (!_context_check(ctx)) {
		handle_list_unlock(_hlist_ctx);
		return SCARD_E_INVALID_HANDLE;
	}

	handle_list_unlock(_hlist_ctx);

	// do nothing

	return SCARD_S_SUCCESS;
}

/* Smart Card and Reader Access Functions */

LONG WINAPI SCardConnectA(SCARDCONTEXT hContext, LPCSTR szReader, DWORD dwShareMode, DWORD dwPreferredProtocols, LPSCARDHANDLE phCard, LPDWORD pdwActiveProtocol)
{
	dbg("SCardConnectA(ITE)");

	if (szReader == NULL || phCard == NULL || pdwActiveProtocol == NULL)
		return SCARD_E_INVALID_PARAMETER;

	struct _context *ctx;

	handle_list_lock(_hlist_ctx);

	handle_list_get_nolock(_hlist_ctx, hContext, &ctx);
	if (!_context_check(ctx)) {
		handle_list_unlock(_hlist_ctx);
		return SCARD_E_INVALID_HANDLE;
	}

	EnterCriticalSection(&ctx->sct);
	handle_list_unlock(_hlist_ctx);

	LONG r = SCARD_F_INTERNAL_ERROR;
	uintptr_t pos = 0;

	while (1)
	{
		struct _reader_device *dev;
		uint32_t id = 0;

		*phCard = 0;
		*pdwActiveProtocol = SCARD_PROTOCOL_UNDEFINED;

		if (_get_reader_id_A(szReader, &pos, &dev, &id) == false) {
			r = SCARD_E_UNKNOWN_READER;
			break;
		}

		struct _handle *handle;

		if (_handle_alloc(&handle) == false) {
			r = SCARD_E_NO_MEMORY;
			break;
		}

		devdb_lock(&dev->db);

		r = _connect_card(handle, dev, id, dwShareMode, dwPreferredProtocols, pdwActiveProtocol);
		if (r == SCARD_S_SUCCESS)
		{
			if (handle_list_put(_hlist_card, handle, phCard) == true) {
				devdb_unlock(&dev->db);
				break;
			}
			else {
				_disconnect_card(handle, false);
				r = SCARD_E_NO_MEMORY;
			}
		}

		devdb_unlock(&dev->db);
		_handle_free(handle);

		pos++;

		if (pos >= _device_num) {
			break;
		}
	}

	LeaveCriticalSection(&ctx->sct);
	return r;
}

LONG WINAPI SCardConnectW(SCARDCONTEXT hContext, LPCWSTR szReader, DWORD dwShareMode, DWORD dwPreferredProtocols, LPSCARDHANDLE phCard, LPDWORD pdwActiveProtocol)
{
	dbg("SCardConnectW(ITE)");

	if (szReader == NULL || phCard == NULL || pdwActiveProtocol == NULL)
		return SCARD_E_INVALID_PARAMETER;

	struct _context *ctx;

	handle_list_lock(_hlist_ctx);

	handle_list_get_nolock(_hlist_ctx, hContext, &ctx);
	if (!_context_check(ctx)) {
		handle_list_unlock(_hlist_ctx);
		return SCARD_E_INVALID_HANDLE;
	}

	EnterCriticalSection(&ctx->sct);
	handle_list_unlock(_hlist_ctx);

	LONG r = SCARD_F_INTERNAL_ERROR;
	uintptr_t pos = 0;

	while (1)
	{
		struct _reader_device *dev;
		uint32_t id = 0;

		*phCard = 0;
		*pdwActiveProtocol = SCARD_PROTOCOL_UNDEFINED;

		if (_get_reader_id_W(szReader, &pos, &dev, &id) == false) {
			r = SCARD_E_UNKNOWN_READER;
			break;
		}

		struct _handle *handle;

		if (_handle_alloc(&handle) == false) {
			r = SCARD_E_NO_MEMORY;
			break;
		}

		devdb_lock(&dev->db);

		r = _connect_card(handle, dev, id, dwShareMode, dwPreferredProtocols, pdwActiveProtocol);
		if (r == SCARD_S_SUCCESS)
		{
			if (handle_list_put(_hlist_card, handle, phCard) == true) {
				devdb_unlock(&dev->db);
				break;
			}
			else {
				_disconnect_card(handle, false);
				r = SCARD_E_NO_MEMORY;
			}
		}

		devdb_unlock(&dev->db);
		_handle_free(handle);

		pos++;

		if (pos >= _device_num) {
			break;
		}
	}

	LeaveCriticalSection(&ctx->sct);
	return r;
}

LONG WINAPI SCardDisconnect(SCARDHANDLE hCard, DWORD dwDisposition)
{
	dbg("SCardDisconnect(ITE)");

	struct _handle *handle;
	LONG r;

	handle_list_lock(_hlist_card);

	handle_list_get_nolock(_hlist_card, hCard, &handle);
	if (!_handle_check(handle)) {
		r = SCARD_E_INVALID_HANDLE;
		goto end;
	}

	EnterCriticalSection(&handle->sct);
	LeaveCriticalSection(&handle->sct);

	bool reset = (dwDisposition & SCARD_RESET_CARD) ? true : false;
	uintptr_t ret = SCARD_F_INTERNAL_ERROR;

	handle_list_release_nolock(_hlist_card, hCard, true, &reset, &ret);

	r = (LONG)ret;

end:
	handle_list_unlock(_hlist_card);
	return r;
}

LONG WINAPI SCardStatusA(SCARDHANDLE hCard, LPSTR szReaderName, LPDWORD pcchReaderLen, LPDWORD pdwState, LPDWORD pdwProtocol, LPBYTE pbAtr, LPDWORD pcbAtrLen)
{
	dbg("SCardStatusA(ITE)");

	struct _handle *handle;
	struct _reader_device *dev;

	handle_list_lock(_hlist_card);

	handle_list_get_nolock(_hlist_card, hCard, &handle);
	if (!_handle_check(handle)) {
		handle_list_unlock(_hlist_card);
		return SCARD_E_INVALID_HANDLE;
	}

	EnterCriticalSection(&handle->sct);
	handle_list_unlock(_hlist_card);

	dev = handle->dev;

	LONG r = SCARD_F_INTERNAL_ERROR;
	bool auto_alloc = false;
	char *name = NULL;

	if (pcchReaderLen != NULL)
	{
		if (szReaderName == NULL) {
			r = SCARD_E_INVALID_PARAMETER;
			goto end2;
		}

		if (*pcchReaderLen == SCARD_AUTOALLOCATE)
		{
			auto_alloc = true;

			name = memAlloc(_READER_NAME_SIZE_A(dev) * sizeof(char));
			if (name == NULL) {
				r = SCARD_E_NO_MEMORY;
				goto end2;
			}

			*((LPSTR *)szReaderName) = name;
		}
		else {
			if (*pcchReaderLen < _READER_NAME_SIZE_A(dev)) {
				r = SCARD_E_INSUFFICIENT_BUFFER;
				goto end2;
			}

			name = szReaderName;
		}

		uint32_t id_len;

		memcpy(name, dev->reader_A, dev->reader_len_A * sizeof(char));
		name[dev->reader_len_A] = ' ';
		id_len = strFromUInt32(name + dev->reader_len_A + 1, 11, handle->id, 10);

		*pcchReaderLen = dev->reader_len_A + 1 + id_len + 1 + 1;

		name[*pcchReaderLen - 1] = '\0';
	}

	devdb_lock(&dev->db);

	r = _get_card_status(handle, pdwState, pdwProtocol);
	if (r != SCARD_S_SUCCESS) {
		devdb_unlock(&dev->db);
		goto end2;
	}

	r = _get_card_atr(&handle->itecard, pbAtr, pcbAtrLen, 32);
	if (r != SCARD_S_SUCCESS) {
		devdb_unlock(&dev->db);
		goto end2;
	}

	devdb_unlock(&dev->db);
	goto end1;

end2:
	if (r != SCARD_S_SUCCESS)
	{
		if (pcchReaderLen != NULL)
		{
			if (auto_alloc == true)
			{
				*((LPSTR *)szReaderName) = NULL;

				if (name != NULL) {
					memFree(name);
				}
			}
			*pcchReaderLen = 0;
		}

		if (pdwState != NULL) {
			*pdwState = SCARD_UNKNOWN;
		}

		if (pdwProtocol != NULL) {
			*pdwProtocol = SCARD_PROTOCOL_UNDEFINED;
		}

		if (pcbAtrLen != NULL)
		{
			if (pbAtr != NULL) {
				if (*pcbAtrLen == SCARD_AUTOALLOCATE) {
					*((LPBYTE *)pbAtr) = NULL;
				}
				else {
					memset(pbAtr, 0, *pcbAtrLen);
				}
			}
			*pcbAtrLen = 0;
		}
	}

end1:
	LeaveCriticalSection(&handle->sct);
	return r;
}

LONG WINAPI SCardStatusW(SCARDHANDLE hCard, LPWSTR szReaderName, LPDWORD pcchReaderLen, LPDWORD pdwState, LPDWORD pdwProtocol, LPBYTE pbAtr, LPDWORD pcbAtrLen)
{
	dbg("SCardStatusW(ITE)");

	struct _handle *handle;
	struct _reader_device *dev;

	handle_list_lock(_hlist_card);

	handle_list_get_nolock(_hlist_card, hCard, &handle);
	if (!_handle_check(handle)) {
		handle_list_unlock(_hlist_card);
		return SCARD_E_INVALID_HANDLE;
	}

	EnterCriticalSection(&handle->sct);
	handle_list_unlock(_hlist_card);

	dev = handle->dev;

	LONG r = SCARD_F_INTERNAL_ERROR;
	bool auto_alloc = false;
	wchar_t *name = NULL;

	if (pcchReaderLen != NULL)
	{
		if (szReaderName == NULL) {
			r = SCARD_E_INVALID_PARAMETER;
			goto end2;
		}

		if (*pcchReaderLen == SCARD_AUTOALLOCATE)
		{
			auto_alloc = true;

			name = memAlloc(_READER_NAME_SIZE_W(dev) * sizeof(wchar_t));
			if (name == NULL) {
				r = SCARD_E_NO_MEMORY;
				goto end2;
			}

			*((LPWSTR *)szReaderName) = name;
		}
		else {
			if (*pcchReaderLen < _READER_NAME_SIZE_W(dev)) {
				r = SCARD_E_INSUFFICIENT_BUFFER;
				goto end2;
			}

			name = szReaderName;
		}

		uint32_t id_len;

		memcpy(name, dev->reader_W, dev->reader_len_W * sizeof(wchar_t));
		name[dev->reader_len_W] = L' ';
		id_len = wstrFromUInt32(name + dev->reader_len_W + 1, 11, handle->id, 10);

		*pcchReaderLen = dev->reader_len_W + 1 + id_len + 1 + 1;

		name[*pcchReaderLen - 1] = L'\0';
	}

	devdb_lock(&dev->db);

	r = _get_card_status(handle, pdwState, pdwProtocol);
	if (r != SCARD_S_SUCCESS) {
		devdb_unlock(&dev->db);
		goto end2;
	}

	r = _get_card_atr(&handle->itecard, pbAtr, pcbAtrLen, 32);
	if (r != SCARD_S_SUCCESS) {
		devdb_unlock(&dev->db);
		goto end2;
	}

	devdb_unlock(&dev->db);
	goto end1;

end2:
	if (r != SCARD_S_SUCCESS)
	{
		if (pcchReaderLen != NULL)
		{
			if (auto_alloc == true)
			{
				*((LPWSTR *)szReaderName) = NULL;

				if (name != NULL) {
					memFree(name);
				}
			}
			*pcchReaderLen = 0;
		}

		if (pdwState != NULL) {
			*pdwState = SCARD_UNKNOWN;
		}

		if (pdwProtocol != NULL) {
			*pdwProtocol = SCARD_PROTOCOL_UNDEFINED;
		}

		if (pcbAtrLen != NULL)
		{
			if (pbAtr != NULL) {
				if (*pcbAtrLen == SCARD_AUTOALLOCATE) {
					*((LPBYTE *)pbAtr) = NULL;
				}
				else {
					memset(pbAtr, 0, *pcbAtrLen);
				}
			}
			*pcbAtrLen = 0;
		}
	}

end1:
	LeaveCriticalSection(&handle->sct);
	return r;
}

LONG WINAPI SCardTransmit(SCARDHANDLE hCard, LPCSCARD_IO_REQUEST pioSendPci, LPCBYTE pbSendBuffer, DWORD cbSendLength, LPSCARD_IO_REQUEST pioRecvPci, LPBYTE pbRecvBuffer, LPDWORD pcbRecvLength)
{
	dbg("SCardTransmit(ITE)");

	if (pioSendPci == NULL || pbSendBuffer == NULL || pbRecvBuffer == NULL || pcbRecvLength == NULL || *pcbRecvLength == SCARD_AUTOALLOCATE)
		return SCARD_E_INVALID_PARAMETER;

	struct _handle *handle;
	struct _reader_device *dev;

	handle_list_lock(_hlist_card);

	handle_list_get_nolock(_hlist_card, hCard, &handle);
	if (!_handle_check(handle)) {
		handle_list_unlock(_hlist_card);
		return SCARD_E_INVALID_HANDLE;
	}

	EnterCriticalSection(&handle->sct);
	handle_list_unlock(_hlist_card);

	dev = handle->dev;

	LONG r;

	devdb_lock(&dev->db);

	switch (pioSendPci->dwProtocol)
	{
	case SCARD_PROTOCOL_T1:
		if (handle->itecard.reader->card.T1.b == false) {
			r = SCARD_E_UNSUPPORTED_FEATURE;
		}
		else {
			r = itecard_status_to_scard_status(itecard_transmit(&handle->itecard, ITECARD_PROTOCOL_T1, pbSendBuffer, cbSendLength, pbRecvBuffer, pcbRecvLength));
		}
		break;

	default:
		r = SCARD_E_READER_UNSUPPORTED;
		break;
	}

	devdb_unlock(&dev->db);

	LeaveCriticalSection(&handle->sct);
	return r;
}

/* Other Functions */

HANDLE WINAPI SCardAccessStartedEvent(void)
{
	if (_hEvent == NULL) {
		_hEvent = CreateEventW(NULL, TRUE, TRUE, NULL);
	}
	else {
		SetEvent(_hEvent);
	}

	return _hEvent;
}

void WINAPI SCardReleaseStartedEvent(void)
{
	return;
}

LONG WINAPI SCardIsValidContext(SCARDCONTEXT hContext)
{
	struct _context *ctx;

	handle_list_lock(_hlist_ctx);

	handle_list_get_nolock(_hlist_ctx, hContext, &ctx);
	if (!_context_check(ctx)) {
		handle_list_unlock(_hlist_ctx);
		return SCARD_E_INVALID_HANDLE;
	}

	handle_list_unlock(_hlist_ctx);

	return SCARD_S_SUCCESS;
}
