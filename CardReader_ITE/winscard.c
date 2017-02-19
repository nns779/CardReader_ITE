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

/* variables */

const SCARD_IO_REQUEST __g_rgSCardT1Pci = { SCARD_PROTOCOL_T1, sizeof(SCARD_IO_REQUEST) };

static HANDLE _hEvent = NULL;

static devdb _devdb_ite;

static handle_list _hlist_ctx;
static handle_list _hlist_card;

static wchar_t _reader_W[128] = { 0 };
static uint32_t _reader_len_W = 0;
static char _reader_A[128] = { 0 };
static uint32_t _reader_len_A = 0;

/* macros */

#define _CONTEXT_BASE	0x8c110000
#define _HANDLE_BASE	0xda910000

#define _CONTEXT_SIGNATURE	0x83fc937b
#define _HANDLE_SIGNATURE	0xa7350c12

#define _context_check_signature(ctx) ((ctx)->signature == _CONTEXT_SIGNATURE)
#define _context_check(ctx) ((ctx) != NULL && _context_check_signature((ctx)))

#define _handle_check_signature(handle) ((handle)->signature == _HANDLE_SIGNATURE)
#define _handle_check(handle) ((handle) != NULL && _handle_check_signature((handle)))

#define _READER_LIST_SIZE_A (((_reader_len_A) + 1 + 10 + 1) * 8 + 1)
#define _READER_LIST_SIZE_W (((_reader_len_W) + 1 + 10 + 1) * 8 + 1)
#define _READER_NAME_SIZE_A ((_reader_len_A) + 1 + 10 + 1 + 1)
#define _READER_NAME_SIZE_W ((_reader_len_W) + 1 + 10 + 1 + 1)

/* structures */

struct _context {
	uint32_t signature;
	CRITICAL_SECTION sct;
};

struct _handle {
	uint32_t signature;
	CRITICAL_SECTION sct;
	uint32_t id;
	struct itecard_handle itecard;
};

struct _reader_list_A
{
	uint32_t size;
	uint32_t len;
	char *list;
};

struct _reader_list_W
{
	uint32_t size;
	uint32_t len;
	wchar_t *list;
};

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

	char name[256];
	uint32_t id_len, name_len;

	memcpy(name, _reader_A, _reader_len_A * sizeof(char));
	name[_reader_len_A] = ' ';
	id_len = strFromUInt32(name + _reader_len_A + 1, 11, id, 10);

	name_len = _reader_len_A + 1 + id_len + 1;

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

	wchar_t name[256];
	uint32_t id_len, name_len;

	memcpy(name, _reader_W, _reader_len_W * sizeof(wchar_t));
	name[_reader_len_W] = L' ';
	id_len = wstrFromUInt32(name + _reader_len_W + 1, 11, id, 10);

	name_len = _reader_len_W + 1 + id_len + 1;

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

static bool _get_reader_id_A(const char *const name, uint32_t *const id)
{
	return (strCompareN(_reader_A, name, _reader_len_A) == false || name[_reader_len_A] != ' ' || strToUInt32(&name[_reader_len_A + 1], id) == false) ? false : true;
}

static bool _get_reader_id_W(const wchar_t *const name, uint32_t *const id)
{
	return (wstrCompareN(_reader_W, name, _reader_len_W) == false || name[_reader_len_W] != ' ' || wstrToUInt32(&name[_reader_len_W + 1], id) == false) ? false : true;
}

static LONG _connect_card(struct _handle *const handle, const uint32_t id, DWORD dwShareMode, DWORD dwPreferredProtocols, LPDWORD pdwActiveProtocol)
{
	bool exclusive;

	if (dwShareMode == SCARD_SHARE_DIRECT)
		return SCARD_E_READER_UNSUPPORTED;

	exclusive = (dwShareMode == SCARD_SHARE_EXCLUSIVE) ? true : false;

	itecard_protocol_t protocol = ITECARD_PROTOCOL_UNDEFINED;

	if (dwPreferredProtocols & SCARD_PROTOCOL_T0)
		protocol |= ITECARD_PROTOCOL_T0;

	if (dwPreferredProtocols & SCARD_PROTOCOL_T1)
		protocol |= ITECARD_PROTOCOL_T1;

	if (protocol == ITECARD_PROTOCOL_UNDEFINED)
		return SCARD_E_READER_UNSUPPORTED;

	LONG r = SCARD_F_INTERNAL_ERROR;
	devdb_status_t dbr;
	struct devdb_shared_devinfo *devinfo;
	struct itecard_shared_readerinfo *reader;

	dbr = devdb_get_shared_devinfo_nolock(&_devdb_ite, id, &devinfo);
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

	if ((dwPreferredProtocols & SCARD_PROTOCOL_T0) && (reader->card.T0.b == true))
		*pdwActiveProtocol |= SCARD_PROTOCOL_T0;

	if ((dwPreferredProtocols & SCARD_PROTOCOL_T1) && (reader->card.T1.b == true))
		*pdwActiveProtocol |= SCARD_PROTOCOL_T1;

	if (*pdwActiveProtocol == SCARD_PROTOCOL_UNDEFINED) {
		internal_err("_connect_card: no active protocol");
		r = SCARD_E_PROTO_MISMATCH;
		goto end2;
	}

	if (devdb_ref_nolock(&_devdb_ite, id, NULL) != DEVDB_S_OK) {
		internal_err("_connect_card: devdb_ref_nolock failed");
		r = SCARD_F_INTERNAL_ERROR;
		goto end2;
	}

	return SCARD_S_SUCCESS;

end2:
	itecard_close(&handle->itecard, false, false);
end1:
	return r;
}

static LONG _disconnect_card(struct _handle *const handle, const bool reset)
{
	uint32_t ref;
	LONG r;

	if (devdb_unref_nolock(&_devdb_ite, handle->id, false, &ref) == DEVDB_S_OK) {
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

static DWORD _get_reader_state(const uint32_t id, LPDWORD pcbAtr, LPBYTE rgbAtr)
{
	DWORD state = 0;
	struct devdb_shared_devinfo *devinfo;

	if (devdb_get_shared_devinfo_nolock(&_devdb_ite, id, &devinfo) != DEVDB_S_OK) {
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

			if ((itecard->protocol & ITECARD_PROTOCOL_T0) && (itecard->reader->card.T0.b == true))
				protocol |= SCARD_PROTOCOL_T0;

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

	devdb_lock(&_devdb_ite);
	r = _disconnect_card(handle, *((bool *)prm));
	devdb_unlock(&_devdb_ite);

	_handle_free(h);

	return (uintptr_t)r;
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
		wchar_t friendlyName[128];
		uint32_t friendlyNameLen;

		ret = GetModuleFileNameW(hinstDLL, path, MAX_PATH + 1);
		if (ret == 0 || wstrCompare(path + ret - 4, L".dll") == false) {
			return FALSE;
		}

		memcpy(path + ret - 3, L"ini", 3 * sizeof(wchar_t));

		_reader_len_W = GetPrivateProfileStringW(L"Setting", L"ReaderName", L"ITE ICC Reader", _reader_W, 128, path);
		_reader_len_A = WideCharToMultiByte(CP_ACP, 0, _reader_W, -1, _reader_A, 128, NULL, NULL);
		if (_reader_len_A == 0) {
			return FALSE;
		}
		_reader_len_A--;

		friendlyNameLen = GetPrivateProfileStringW(L"Setting", L"FriendlyName", L"DigiBest ISDB-T IT9175 BDA Filter", friendlyName, 128, path);

		if (GetPrivateProfileIntW(L"Debug", L"Logging", 0, path) != 0)
		{
			dbg_enable(true);

			if (GetPrivateProfileIntW(L"Debug", L"OutputToFile", 0, path) != 0) {
				memcpy(path + ret - 3, L"log", 3 * sizeof(wchar_t));
				dbg_open(path);
			}
		}

		if (devdb_open(&_devdb_ite, friendlyName, sizeof(struct itecard_shared_readerinfo)) != DEVDB_S_OK) {
			dbg_close();
			memDeinit();
			return FALSE;
		}

		if (handle_list_init(&_hlist_ctx, _CONTEXT_BASE, 32, _context_release_callback) == false) {
			devdb_close(&_devdb_ite);
			dbg_close();
			memDeinit();
			return FALSE;
		}

		if (handle_list_init(&_hlist_card, _HANDLE_BASE, 32, _handle_release_callback) == false) {
			handle_list_deinit(_hlist_ctx);
			devdb_close(&_devdb_ite);
			dbg_close();
			memDeinit();
			return FALSE;
		}

		break;
	}

	case DLL_PROCESS_DETACH:
	{
		handle_list_deinit(_hlist_card);
		handle_list_deinit(_hlist_ctx);
		devdb_close(&_devdb_ite);
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

			rl.list = memAlloc(_READER_LIST_SIZE_A * sizeof(char));
			if (rl.list == NULL) {
				internal_err("SCardListReadersA(ITE): memAlloc failed");
				r = SCARD_E_NO_MEMORY;
				goto end;
			}

			rl.size = _READER_LIST_SIZE_A - 1;

			*((LPSTR *)mszReaders) = rl.list;
		}
		else {
			rl.list = mszReaders;
			rl.size = *pcchReaders - 1;
		}
	}

	devdb_status_t ret;

	devdb_lock(&_devdb_ite);

	ret = devdb_update_nolock(&_devdb_ite);
	if (ret == DEVDB_S_OK)
	{
		ret = devdb_enum_nolock(&_devdb_ite, _enum_readers_callback_A, &rl);
		if (ret != DEVDB_S_OK) {
			r = SCARD_F_INTERNAL_ERROR;
		}
		else if (rl.len == 0) {
			r = SCARD_E_INSUFFICIENT_BUFFER;
		}
		else
		{
			if (rl.list != NULL) {
				rl.list[rl.len] = '\0';
			}

			*pcchReaders = rl.len + 1;

			r = SCARD_S_SUCCESS;
		}
	}
	else {
		r = devdb_status_to_scard_status(ret);
	}

	devdb_unlock(&_devdb_ite);

end:
	if (r != SCARD_S_SUCCESS)
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

			rl.list = memAlloc(_READER_LIST_SIZE_W * sizeof(wchar_t));
			if (rl.list == NULL) {
				internal_err("SCardListReadersW(ITE): memAlloc failed");
				r = SCARD_E_NO_MEMORY;
				goto end;
			}

			rl.size = _READER_LIST_SIZE_W - 1;

			*((LPWSTR *)mszReaders) = rl.list;
		}
		else {
			rl.list = mszReaders;
			rl.size = *pcchReaders - 1;
		}
	}

	devdb_status_t ret;

	devdb_lock(&_devdb_ite);

	ret = devdb_update_nolock(&_devdb_ite);
	if (ret == DEVDB_S_OK)
	{
		ret = devdb_enum_nolock(&_devdb_ite, _enum_readers_callback_W, &rl);
		if (ret != DEVDB_S_OK) {
			r = SCARD_F_INTERNAL_ERROR;
		}
		else if (rl.len == 0) {
			r = SCARD_E_INSUFFICIENT_BUFFER;
		}
		else
		{
			if (rl.list != NULL) {
				rl.list[rl.len] = L'\0';
			}

			*pcchReaders = rl.len + 1;

			r = SCARD_S_SUCCESS;
		}
	}
	else {
		r = devdb_status_to_scard_status(ret);
	}

	devdb_unlock(&_devdb_ite);

end:
	if (r != SCARD_S_SUCCESS)
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

	devdb_lock(&_devdb_ite);

	for (uint32_t i = 0; i < cReaders; i++)
	{
		if (strCompare(rgReaderStates[i].szReader, "\\\\?PnP?\\Notification") == true) {
			r = SCARD_E_READER_UNSUPPORTED;
			break;
		}

		rgReaderStates[i].cbAtr = 0;

		DWORD state = 0;
		uint32_t id;

		if (_get_reader_id_A(rgReaderStates[i].szReader, &id) == false) {
			state = SCARD_STATE_IGNORE | SCARD_STATE_UNKNOWN;
		}
		else {
			state = _get_reader_state(id, &rgReaderStates[i].cbAtr, rgReaderStates[i].rgbAtr);
		}

		rgReaderStates[i].dwEventState = state | ((state != rgReaderStates[i].dwCurrentState) ? SCARD_STATE_CHANGED : 0);
	}

	devdb_unlock(&_devdb_ite);
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

	devdb_lock(&_devdb_ite);

	for (uint32_t i = 0; i < cReaders; i++)
	{
		if (wstrCompare(rgReaderStates[i].szReader, L"\\\\?PnP?\\Notification") == true) {
			r = SCARD_E_READER_UNSUPPORTED;
			break;
		}

		rgReaderStates[i].cbAtr = 0;

		DWORD state = 0;
		uint32_t id;

		if (_get_reader_id_W(rgReaderStates[i].szReader, &id) == false) {
			state = SCARD_STATE_IGNORE | SCARD_STATE_UNKNOWN;
		}
		else {
			state = _get_reader_state(id, &rgReaderStates[i].cbAtr, rgReaderStates[i].rgbAtr);
		}

		rgReaderStates[i].dwEventState = state | ((state != rgReaderStates[i].dwCurrentState) ? SCARD_STATE_CHANGED : 0);
	}

	devdb_unlock(&_devdb_ite);
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
	uint32_t id = 0;

	*phCard = 0;
	*pdwActiveProtocol = SCARD_PROTOCOL_UNDEFINED;

	if (_get_reader_id_A(szReader, &id) == false) {
		r = SCARD_E_UNKNOWN_READER;
		goto end1;
	}

	struct _handle *handle;

	if (_handle_alloc(&handle) == false) {
		r = SCARD_E_NO_MEMORY;
		goto end1;
	}

	devdb_lock(&_devdb_ite);

	r = _connect_card(handle, id, dwShareMode, dwPreferredProtocols, pdwActiveProtocol);
	if (r == SCARD_S_SUCCESS)
	{
		if (handle_list_put(_hlist_card, handle, phCard) == true) {
			goto end2;
		}
		else {
			r = SCARD_E_NO_MEMORY;
		}
	}

	_handle_free(handle);

end2:
	devdb_unlock(&_devdb_ite);
end1:
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
	uint32_t id = 0;

	*phCard = 0;
	*pdwActiveProtocol = SCARD_PROTOCOL_UNDEFINED;

	if (_get_reader_id_W(szReader, &id) == false) {
		r = SCARD_E_UNKNOWN_READER;
		goto end1;
	}

	struct _handle *handle;

	if (_handle_alloc(&handle) == false) {
		r = SCARD_E_NO_MEMORY;
		goto end1;
	}

	devdb_lock(&_devdb_ite);

	r = _connect_card(handle, id, dwShareMode, dwPreferredProtocols, pdwActiveProtocol);
	if (r == SCARD_S_SUCCESS)
	{
		if (handle_list_put(_hlist_card, handle, phCard) == true) {
			goto end2;
		}
		else {
			r = SCARD_E_NO_MEMORY;
		}
	}

	_handle_free(handle);

end2:
	devdb_unlock(&_devdb_ite);
end1:
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

	handle_list_lock(_hlist_card);

	handle_list_get_nolock(_hlist_card, hCard, &handle);
	if (!_handle_check(handle)) {
		handle_list_unlock(_hlist_card);
		return SCARD_E_INVALID_HANDLE;
	}

	EnterCriticalSection(&handle->sct);
	handle_list_unlock(_hlist_card);

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

			name = memAlloc(_READER_NAME_SIZE_A * sizeof(char));
			if (name == NULL) {
				r = SCARD_E_NO_MEMORY;
				goto end2;
			}

			*((LPSTR *)szReaderName) = name;
		}
		else {
			if (*pcchReaderLen < _READER_NAME_SIZE_A) {
				r = SCARD_E_INSUFFICIENT_BUFFER;
				goto end2;
			}

			name = szReaderName;
		}

		uint32_t id_len;

		memcpy(name, _reader_A, _reader_len_A * sizeof(char));
		name[_reader_len_A] = ' ';
		id_len = strFromUInt32(name + _reader_len_A + 1, 11, handle->id, 10);

		*pcchReaderLen = _reader_len_A + 1 + id_len + 1 + 1;

		name[*pcchReaderLen - 1] = '\0';
	}

	devdb_lock(&_devdb_ite);

	r = _get_card_status(handle, pdwState, pdwProtocol);
	if (r != SCARD_S_SUCCESS) {
		devdb_unlock(&_devdb_ite);
		goto end2;
	}

	r = _get_card_atr(&handle->itecard, pbAtr, pcbAtrLen, 32);
	if (r != SCARD_S_SUCCESS) {
		devdb_unlock(&_devdb_ite);
		goto end2;
	}

	devdb_unlock(&_devdb_ite);
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
				memset(pbAtr, 0, 32);
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

	handle_list_lock(_hlist_card);

	handle_list_get_nolock(_hlist_card, hCard, &handle);
	if (!_handle_check(handle)) {
		handle_list_unlock(_hlist_card);
		return SCARD_E_INVALID_HANDLE;
	}

	EnterCriticalSection(&handle->sct);
	handle_list_unlock(_hlist_card);

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

			name = memAlloc(_READER_NAME_SIZE_W * sizeof(wchar_t));
			if (name == NULL) {
				r = SCARD_E_NO_MEMORY;
				goto end2;
			}

			*((LPWSTR *)szReaderName) = name;
		}
		else {
			if (*pcchReaderLen < _READER_NAME_SIZE_W) {
				r = SCARD_E_INSUFFICIENT_BUFFER;
				goto end2;
			}

			name = szReaderName;
		}

		uint32_t id_len;

		memcpy(name, _reader_W, _reader_len_W * sizeof(wchar_t));
		name[_reader_len_W] = L' ';
		id_len = wstrFromUInt32(name + _reader_len_W + 1, 11, handle->id, 10);

		*pcchReaderLen = _reader_len_W + 1 + id_len + 1 + 1;

		name[*pcchReaderLen - 1] = L'\0';
	}

	devdb_lock(&_devdb_ite);

	r = _get_card_status(handle, pdwState, pdwProtocol);
	if (r != SCARD_S_SUCCESS) {
		devdb_unlock(&_devdb_ite);
		goto end2;
	}

	r = _get_card_atr(&handle->itecard, pbAtr, pcbAtrLen, 32);
	if (r != SCARD_S_SUCCESS) {
		devdb_unlock(&_devdb_ite);
		goto end2;
	}

	devdb_unlock(&_devdb_ite);
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
				memset(pbAtr, 0, 32);
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

	handle_list_lock(_hlist_card);

	handle_list_get_nolock(_hlist_card, hCard, &handle);
	if (!_handle_check(handle)) {
		handle_list_unlock(_hlist_card);
		return SCARD_E_INVALID_HANDLE;
	}

	EnterCriticalSection(&handle->sct);
	handle_list_unlock(_hlist_card);

	LONG r;

	devdb_lock(&_devdb_ite);

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

	devdb_unlock(&_devdb_ite);

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
