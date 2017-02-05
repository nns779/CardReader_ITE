// winscard.c

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <windows.h>

#include "debug.h"
#include "memory.h"
#include "string.h"
#include "itecard.h"

static wchar_t readerNameW[128] = { 0 };
static uint32_t readerNameLenW = 0;
static char readerNameA[128] = { 0 };
static uint32_t readerNameLenA = 0;
static wchar_t friendlyName[128] = { 0 };
static uint32_t friendlyNameLen = 0;

#define _scard_READER_LIST_SIZE_A (((readerNameLenA) + 12) * 8 + 1)
#define _scard_READER_LIST_SIZE_W (((readerNameLenW) + 12) * 8 + 1)
#define _scard_READER_NAME_SIZE_A ((readerNameLenA) + 12 + 1)
#define _scard_READER_NAME_SIZE_W ((readerNameLenW) + 12 + 1)

const SCARD_IO_REQUEST _g_rgSCardT1Pci = { SCARD_PROTOCOL_T1, sizeof(SCARD_IO_REQUEST) };

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
	switch (fdwReason)
	{
	case DLL_PROCESS_ATTACH:
	{
		wchar_t path[MAX_PATH + 1];
		DWORD ret;

		ret = GetModuleFileNameW(hinstDLL, path, MAX_PATH + 1);
		if (ret == 0 || wstrCompare(path + ret - 4, L".dll") == false) {
			return FALSE;
		}

		memcpy(path + ret - 3, L"ini", 3 * sizeof(wchar_t));

		readerNameLenW = GetPrivateProfileStringW(L"Setting", L"ReaderName", L"ITE ICC Reader", readerNameW, 128, path);
		readerNameLenA = WideCharToMultiByte(CP_ACP, 0, readerNameW, -1, readerNameA, 128, NULL, NULL);
		if (readerNameLenA == 0) {
			return FALSE;
		}
		readerNameLenA--;

		friendlyNameLen = GetPrivateProfileStringW(L"Setting", L"FriendlyName", L"DigiBest ISDB-T IT9175 BDA Filter", friendlyName, 128, path);

		if (memInit() == false) return FALSE;
		DisableThreadLibraryCalls(hinstDLL);

		break;
	}

	case DLL_PROCESS_DETACH:
	{
		memDeinit();
		break;
	}

	default:
		break;
	}

	return TRUE;
}

LONG itecard_status_to_scard_status(itecard_status_t status)
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
		dbg(L"itecard_status_to_scard_status: %d", status);
		return SCARD_F_INTERNAL_ERROR;
	}
}

bool _scardReaderNameToIdA(const char *const name, uint32_t *const id)
{
	return (strCompareN(readerNameA, name, readerNameLenA) == false || name[readerNameLenA] != ' ' || strToUInt32(&name[readerNameLenA + 1], id) == false) ? false : true;
}

bool _scardReaderNameToIdW(const wchar_t *const name, uint32_t *const id)
{
	return (wstrCompareN(readerNameW, name, readerNameLenW) == false || name[readerNameLenW] != ' ' || wstrToUInt32(&name[readerNameLenW + 1], id) == false) ? false : true;
}

LONG _scardConnect(struct itecard_devlist *const dl, const uint32_t id, DWORD dwShareMode, DWORD dwPreferredProtocols, LPSCARDHANDLE phCard, LPDWORD pdwActiveProtocol)
{
	if (dwShareMode == SCARD_SHARE_DIRECT)
		return SCARD_E_READER_UNSUPPORTED;

	itecard_protocol_t protocol = ITECARD_PROTOCOL_UNDEFINED;

	if (dwPreferredProtocols & SCARD_PROTOCOL_T0)
		protocol |= ITECARD_PROTOCOL_T0;

	if (dwPreferredProtocols & SCARD_PROTOCOL_T1)
		protocol |= ITECARD_PROTOCOL_T1;

	if (protocol == ITECARD_PROTOCOL_UNDEFINED)
		return SCARD_E_READER_UNSUPPORTED;

	struct itecard_handle *handle;

	handle = memAlloc(sizeof(struct itecard_handle));
	if (handle == NULL) {
		internal_err(L"_scardConnect: memAlloc failed");
		return SCARD_E_NO_MEMORY;
	}

	itecard_status_t ret;

	ret = itecard_devlist_register(dl, id);
	if (ret != ITECARD_S_OK) {
		internal_err(L"_scardConnect: itecard_devlist_register failed");
		memFree(handle);
		return itecard_status_to_scard_status(ret);
	}

	ret = itecard_open(handle, friendlyName, id, protocol, (dwShareMode == SCARD_SHARE_EXCLUSIVE) ? true : false);
	if (ret != ITECARD_S_OK) {
		internal_err(L"_scardConnect: itecard_open failed");
		memFree(handle);
		return itecard_status_to_scard_status(ret);
	}

	ret = itecard_init(handle);
	if (ret != ITECARD_S_OK && ret != ITECARD_S_FALSE) {
		internal_err(L"_scardConnect: itecard_init failed");
		itecard_close(handle, false);
		memFree(handle);
		return itecard_status_to_scard_status(ret);
	}

	if ((dwPreferredProtocols & SCARD_PROTOCOL_T0) && (handle->dev->card.T0.b == true))
		*pdwActiveProtocol |= SCARD_PROTOCOL_T0;

	if ((dwPreferredProtocols & SCARD_PROTOCOL_T1) && (handle->dev->card.T1.b == true))
		*pdwActiveProtocol |= SCARD_PROTOCOL_T1;

	if (*pdwActiveProtocol == SCARD_PROTOCOL_UNDEFINED) {
		internal_err(L"_scardConnect: no active protocol");
		itecard_close(handle, false);
		memFree(handle);
		return SCARD_E_PROTO_MISMATCH;
	}

	*phCard = (SCARDHANDLE)handle;

	return SCARD_S_SUCCESS;
}

LONG WINAPI SCardConnectA(SCARDCONTEXT hContext, LPCSTR szReader, DWORD dwShareMode, DWORD dwPreferredProtocols, LPSCARDHANDLE phCard, LPDWORD pdwActiveProtocol)
{
	dbg(L"SCardConnectA(ITE)");

	struct itecard_devlist *dl = (struct itecard_devlist *)hContext;

	if (!itecard_devlist_check(dl))
		return SCARD_E_INVALID_HANDLE;

	if (szReader == NULL || phCard == NULL || pdwActiveProtocol == NULL)
		return SCARD_E_INVALID_PARAMETER;

	*pdwActiveProtocol = SCARD_PROTOCOL_UNDEFINED;

	uint32_t id = 0;

	if (_scardReaderNameToIdA(szReader, &id) == false)
		return SCARD_E_UNKNOWN_READER;

	return _scardConnect(dl, id, dwShareMode, dwPreferredProtocols, phCard, pdwActiveProtocol);
}

LONG WINAPI SCardConnectW(SCARDCONTEXT hContext, LPCWSTR szReader, DWORD dwShareMode, DWORD dwPreferredProtocols, LPSCARDHANDLE phCard, LPDWORD pdwActiveProtocol)
{
	dbg(L"SCardConnectW(ITE)");

	struct itecard_devlist *dl = (struct itecard_devlist *)hContext;

	if (!itecard_devlist_check(dl))
		return SCARD_E_INVALID_HANDLE;

	if (szReader == NULL || phCard == NULL || pdwActiveProtocol == NULL)
		return SCARD_E_INVALID_PARAMETER;

	*pdwActiveProtocol = SCARD_PROTOCOL_UNDEFINED;

	uint32_t id = 0;

	if (_scardReaderNameToIdW(szReader, &id) == false)
		return SCARD_E_UNKNOWN_READER;

	return _scardConnect(dl, id, dwShareMode, dwPreferredProtocols, phCard, pdwActiveProtocol);
}

LONG WINAPI SCardDisconnect(SCARDHANDLE hCard, DWORD dwDisposition)
{
	dbg(L"SCardDisconnect(ITE)");

	struct itecard_handle *handle = (struct itecard_handle *)hCard;

	if (!itecard_handle_check(handle))
		return SCARD_E_INVALID_HANDLE;

	LONG r;

	r = itecard_status_to_scard_status(itecard_close(handle, (dwDisposition & SCARD_RESET_CARD) ? true : false));
	memFree(handle);

	return r;
}

LONG WINAPI SCardEstablishContext(DWORD dwScope, LPCVOID pvReserved1, LPCVOID pvReserved2, LPSCARDCONTEXT phContext)
{
	dbg(L"SCardEstablishContext(ITE)");

	if (phContext == NULL)
		return SCARD_E_INVALID_PARAMETER;

	struct itecard_devlist *dl;

	dl = memAlloc(sizeof(struct itecard_devlist));
	if (dl == NULL)
		return SCARD_E_NO_MEMORY;

	itecard_status_t ret;

	ret = itecard_devlist_open(dl, friendlyName);
	if (ret != ITECARD_S_OK) {
		internal_err(L"SCardEstablishContext(ITE): itecard_devlist_open failed");
		return itecard_status_to_scard_status(ret);
	}

	*phContext = (SCARDCONTEXT)dl;

	return SCARD_S_SUCCESS;
}

LONG WINAPI SCardFreeMemory(SCARDCONTEXT hContext, LPCVOID pvMem)
{
	struct itecard_devlist *dl = (struct itecard_devlist *)hContext;

	if (dl != NULL && !itecard_devlist_check_signature(dl))
		return SCARD_E_INVALID_HANDLE;

	memFree(pvMem);

	return SCARD_S_SUCCESS;
}

DWORD _scardGetState(struct itecard_devlist *const devlist, const uint32_t id)
{
	DWORD state = 0;

	if (itecard_devlist_register(devlist, id) != ITECARD_S_OK) {
		state = SCARD_STATE_UNAVAILABLE;
	}
	else {
		struct itecard_handle h;
		bool b;

		if (itecard_open(&h, friendlyName, id, ITECARD_PROTOCOL_UNDEFINED, false) != ITECARD_S_OK || itecard_detect(&h, &b) != ITECARD_S_OK) {
			state = SCARD_STATE_UNAVAILABLE;
		}
		else {
			state = (b == true) ? SCARD_STATE_PRESENT : SCARD_STATE_EMPTY;
			if (b == true && h.dev->ref > 1) {
				state |= (h.dev->exclusive == true) ? SCARD_STATE_EXCLUSIVE : SCARD_STATE_INUSE;
			}
		}

		if (itecard_handle_check_signature(&h))
			itecard_close(&h, false);
	}

	return state;
}

LONG WINAPI SCardGetStatusChangeA(SCARDCONTEXT hContext, DWORD dwTimeout, LPSCARD_READERSTATEA rgReaderStates, DWORD cReaders)
{
	dbg(L"SCardGetStatusChangeA(ITE)");

	struct itecard_devlist *dl = (struct itecard_devlist *)hContext;

	if (!itecard_devlist_check(dl))
		return SCARD_E_INVALID_HANDLE;

	if (rgReaderStates == NULL || cReaders == 0)
		return SCARD_E_INVALID_PARAMETER;

	if (dwTimeout != 0)
		return SCARD_E_READER_UNSUPPORTED;

	for (uint32_t i = 0; i < cReaders; i++)
	{
		if (strCompare(rgReaderStates[i].szReader, "\\\\?PnP?\\Notification") == true) {
			return SCARD_E_READER_UNSUPPORTED;
		}

		DWORD state = 0;
		uint32_t id;

		if (_scardReaderNameToIdA(rgReaderStates[i].szReader, &id) == false) {
			state = SCARD_STATE_IGNORE | SCARD_STATE_UNKNOWN;
		}
		else {
			state = _scardGetState(dl, id);
		}

		if (state != rgReaderStates[i].dwCurrentState) {
			rgReaderStates[i].dwEventState = state | SCARD_STATE_CHANGED;
		}

		rgReaderStates[i].cbAtr = 0;
	}

	return SCARD_S_SUCCESS;
}

LONG WINAPI SCardGetStatusChangeW(SCARDCONTEXT hContext, DWORD dwTimeout, LPSCARD_READERSTATEW rgReaderStates, DWORD cReaders)
{
	dbg(L"SCardGetStatusChangeW(ITE)");

	struct itecard_devlist *dl = (struct itecard_devlist *)hContext;

	if (!itecard_devlist_check(dl))
		return SCARD_E_INVALID_HANDLE;

	if (rgReaderStates == NULL || cReaders == 0)
		return SCARD_E_INVALID_PARAMETER;

	if (dwTimeout != 0)
		return SCARD_E_READER_UNSUPPORTED;

	for (uint32_t i = 0; i < cReaders; i++)
	{
		if (wstrCompare(rgReaderStates[i].szReader, L"\\\\?PnP?\\Notification") == true) {
			return SCARD_E_READER_UNSUPPORTED;
		}

		DWORD state = 0;
		uint32_t id;

		if (_scardReaderNameToIdW(rgReaderStates[i].szReader, &id) == false) {
			state = SCARD_STATE_IGNORE | SCARD_STATE_UNKNOWN;
		}
		else {
			state = _scardGetState(dl, id);
		}

		if (state != rgReaderStates[i].dwCurrentState) {
			rgReaderStates[i].dwEventState = state | SCARD_STATE_CHANGED;
		}

		rgReaderStates[i].cbAtr = 0;
	}

	return SCARD_S_SUCCESS;
}

LONG WINAPI SCardIsValidContext(SCARDCONTEXT hContext)
{
	dbg(L"SCardIsValidContext(ITE)");

	struct itecard_devlist *dl = (struct itecard_devlist *)hContext;

	if (!itecard_devlist_check(dl))
		return SCARD_E_INVALID_HANDLE;

	return SCARD_S_SUCCESS;
}

struct _scardListReadersDataA
{
	uint32_t size;
	uint32_t len;
	char *list;
};

int _scardListReadersCallbackA(struct itecard_devlist *const devlist, const uint32_t id, const wchar_t *const path, void *prm)
{
	struct _scardListReadersDataA *d = prm;

	char name[256];
	uint32_t name_len;

	sprintf_s(name, _scard_READER_NAME_SIZE_A - 1, "%s %d", readerNameA, id);
	name_len = strLen(name) + 1;

	if (d->list != NULL)
	{
		if (d->len + name_len > d->size) {
			return 0;
		}
		memcpy(d->list + d->len, name, name_len * sizeof(char));
	}

	d->len += name_len;

	return 1;
}

LONG WINAPI SCardListReadersA(SCARDCONTEXT hContext, LPCSTR mszGroups, LPSTR mszReaders, LPDWORD pcchReaders)
{
	dbg(L"SCardListReadersA(ITE)");

	struct itecard_devlist *dl = (struct itecard_devlist *)hContext;

	if (!itecard_devlist_check(dl))
		return SCARD_E_INVALID_HANDLE;

	if (pcchReaders == NULL)
		return SCARD_E_INVALID_PARAMETER;

	bool auto_alloc = false;
	struct _scardListReadersDataA d;

	d.size = 0;
	d.len = 0;
	d.list = NULL;

	if (mszReaders != NULL)
	{
		if (*pcchReaders == SCARD_AUTOALLOCATE)
		{
			auto_alloc = true;

			d.list = memAlloc(_scard_READER_LIST_SIZE_A * sizeof(char));
			if (d.list == NULL) {
				internal_err(L"SCardListReadersA(ITE): memAlloc failed");
				*pcchReaders = 0;
				return SCARD_E_NO_MEMORY;
			}

			d.size = _scard_READER_LIST_SIZE_A;
		}
		else if (*pcchReaders < _scard_READER_LIST_SIZE_A) {
			*pcchReaders = _scard_READER_LIST_SIZE_A;
			return SCARD_E_INSUFFICIENT_BUFFER;
		}
		else {
			d.list = mszReaders;
			d.size = *pcchReaders;
		}
	}

	itecard_status_t ret;
	LONG r;

	ret = itecard_devlist_enum(dl, _scardListReadersCallbackA, &d);
	if (ret == ITECARD_S_OK || ret == ITECARD_E_NO_MORE_DATA)
	{
		if (d.len == 0) {
			r = SCARD_E_NO_READERS_AVAILABLE;
		}
		else {
			if (d.list != NULL)
			{
				d.list[d.len] = '\0';

				if (auto_alloc == true) {
					*((LPSTR *)mszReaders) = d.list;
				}

				*pcchReaders = d.len + 1;
			}
			else {
				*pcchReaders = _scard_READER_LIST_SIZE_A;
			}

			r = SCARD_S_SUCCESS;
		}
	}
	else {
		dbg(L"SCardListReadersA(ITE): itecard_devlist_enum failed (%08X)", ret);
		r = itecard_status_to_scard_status(ret);
	}

	if (r != SCARD_S_SUCCESS) {
		if (auto_alloc == true) {
			memFree(d.list);
			*((LPSTR *)mszReaders) = NULL;
		}
		*pcchReaders = 0;
	}

	return r;
}

struct _scardListReadersDataW
{
	uint32_t size;
	uint32_t len;
	wchar_t *list;
};

int _scardListReadersCallbackW(struct itecard_devlist *const devlist, const uint32_t id, const wchar_t *const path, void *prm)
{
	struct _scardListReadersDataW *d = prm;

	wchar_t name[256];
	uint32_t name_len;

	swprintf_s(name, _scard_READER_NAME_SIZE_W - 1, L"%s %d", readerNameW, id);
	name_len = wstrLen(name) + 1;

	if (d->list != NULL)
	{
		if (d->len + name_len > d->size) {
			return 0;
		}
		memcpy(d->list + d->len, name, name_len * sizeof(wchar_t));
	}

	d->len += name_len;

	return 1;
}

LONG WINAPI SCardListReadersW(SCARDCONTEXT hContext, LPCWSTR mszGroups, LPWSTR mszReaders, LPDWORD pcchReaders)
{
	dbg(L"SCardListReadersW(ITE)");

	struct itecard_devlist *dl = (struct itecard_devlist *)hContext;

	if (!itecard_devlist_check(dl))
		return SCARD_E_INVALID_HANDLE;

	if (pcchReaders == NULL)
		return SCARD_E_INVALID_PARAMETER;

	bool auto_alloc = false;
	struct _scardListReadersDataW d;

	d.size = 0;
	d.len = 0;
	d.list = NULL;

	if (mszReaders != NULL)
	{
		if (*pcchReaders == SCARD_AUTOALLOCATE)
		{
			auto_alloc = true;

			d.list = memAlloc(_scard_READER_LIST_SIZE_W * sizeof(wchar_t));
			if (d.list == NULL) {
				internal_err(L"SCardListReadersW(ITE): memAlloc failed");
				*pcchReaders = 0;
				return SCARD_E_NO_MEMORY;
			}

			d.size = _scard_READER_LIST_SIZE_W;
		}
		else if (*pcchReaders < _scard_READER_LIST_SIZE_W) {
			*pcchReaders = _scard_READER_LIST_SIZE_W;
			return SCARD_E_INSUFFICIENT_BUFFER;
		}
		else {
			d.list = mszReaders;
			d.size = *pcchReaders;
		}
	}

	itecard_status_t ret;
	LONG r;

	ret = itecard_devlist_enum(dl, _scardListReadersCallbackW, &d);
	if (ret == ITECARD_S_OK || ret == ITECARD_E_NO_MORE_DATA)
	{
		if (d.len == 0) {
			r = SCARD_E_NO_READERS_AVAILABLE;
		}
		else {
			if (d.list != NULL)
			{
				d.list[d.len] = L'\0';

				if (auto_alloc == true) {
					*((LPWSTR *)mszReaders) = d.list;
				}

				*pcchReaders = d.len + 1;
			}
			else {
				*pcchReaders = _scard_READER_LIST_SIZE_W;
			}

			r = SCARD_S_SUCCESS;
		}
	}
	else {
		dbg(L"SCardListReadersW(ITE): itecard_devlist_enum failed (%08X)", ret);
		r = itecard_status_to_scard_status(ret);
	}

	if (r != SCARD_S_SUCCESS) {
		if (auto_alloc == true) {
			memFree(d.list);
			*((LPWSTR *)mszReaders) = NULL;
		}
		*pcchReaders = 0;
	}

	return r;
}

LONG WINAPI SCardReleaseContext(SCARDCONTEXT hContext)
{
	dbg(L"SCardReleaseContext(ITE)");

	struct itecard_devlist *dl = (struct itecard_devlist *)hContext;

	if (!itecard_devlist_check(dl))
		return SCARD_E_INVALID_HANDLE;

	itecard_devlist_close(dl);
	memFree(dl);

	return SCARD_S_SUCCESS;
}

LONG _scardStatus(struct itecard_handle *const handle, LPDWORD pdwState, LPDWORD pdwProtocol, LPBYTE pbAtr, LPDWORD pcbAtrLen)
{
	itecard_status_t ret;

	ret = itecard_init(handle);
	if (ret == ITECARD_E_FAILED) {
		internal_err(L"_scardStatus: itecard_init failed");
		return SCARD_F_INTERNAL_ERROR;
	}

	if (pdwState != NULL)
	{
		if (ret == ITECARD_E_NO_CARD) {
			*pdwState = SCARD_ABSENT;
		}
		else if (ret == ITECARD_S_OK || ret == ITECARD_S_FALSE)
		{
			DWORD protocol = SCARD_PROTOCOL_UNDEFINED;

			if ((handle->protocol & ITECARD_PROTOCOL_T0) && (handle->dev->card.T0.b == true))
				protocol |= SCARD_PROTOCOL_T0;

			if ((handle->protocol & ITECARD_PROTOCOL_T1) && (handle->dev->card.T1.b == true))
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

	if (pcbAtrLen != NULL)
	{
		if (pbAtr == NULL)
			return SCARD_E_INVALID_PARAMETER;

		struct card_info *card = &handle->dev->card;

		if (card->atr_len == 0) {
			*pcbAtrLen = 0;
		}
		else
		{
			uint8_t *atr;

			if (*pcbAtrLen == SCARD_AUTOALLOCATE) {
				atr = memAlloc(card->atr_len);
				if (atr == NULL)
					return SCARD_E_NO_MEMORY;

				*((LPBYTE *)pbAtr) = atr;
			}
			else {
				if (*pcbAtrLen < card->atr_len)
					return SCARD_E_INSUFFICIENT_BUFFER;

				atr = pbAtr;
			}

			memcpy(atr, card->atr, card->atr_len);
			*pcbAtrLen = card->atr_len;
		}
	}

	return SCARD_S_SUCCESS;
}

LONG WINAPI SCardStatusA(SCARDHANDLE hCard, LPSTR szReaderName, LPDWORD pcchReaderLen, LPDWORD pdwState, LPDWORD pdwProtocol, LPBYTE pbAtr, LPDWORD pcbAtrLen)
{
	dbg(L"SCardStatusA(ITE)");

	struct itecard_handle *handle = (struct itecard_handle *)hCard;

	if (!itecard_handle_check(handle))
		return SCARD_E_INVALID_HANDLE;

	bool auto_alloc = false;
	char *name = NULL;

	if (pcchReaderLen != NULL)
	{
		if (szReaderName == NULL)
			return SCARD_E_INVALID_PARAMETER;

		if (*pcchReaderLen == SCARD_AUTOALLOCATE) {
			name = memAlloc(_scard_READER_NAME_SIZE_A * sizeof(char));
			if (name == NULL) {
				*((LPSTR*)szReaderName) = NULL;
				return SCARD_E_NO_MEMORY;
			}

			*((LPSTR *)szReaderName) = name;
			auto_alloc = true;
		}
		else {
			if (*pcchReaderLen < _scard_READER_NAME_SIZE_A)
				return SCARD_E_INSUFFICIENT_BUFFER;

			name = szReaderName;
		}

		sprintf_s(name, _scard_READER_NAME_SIZE_A, "%s %d", readerNameA, ((struct itecard_handle *)hCard)->id);
		*pcchReaderLen = strLen(name) + 2;

		name[*pcchReaderLen - 1] = '\0';
	}

	LONG r;

	r = _scardStatus(handle, pdwState, pdwProtocol, pbAtr, pcbAtrLen);
	if (r != SCARD_S_SUCCESS && auto_alloc == true) {
		memFree(name);
		*((LPSTR*)szReaderName) = NULL;
	}

	return r;
}

LONG WINAPI SCardStatusW(SCARDHANDLE hCard, LPWSTR szReaderName, LPDWORD pcchReaderLen, LPDWORD pdwState, LPDWORD pdwProtocol, LPBYTE pbAtr, LPDWORD pcbAtrLen)
{
	dbg(L"SCardStatusW(ITE)");

	struct itecard_handle *handle = (struct itecard_handle *)hCard;

	if (!itecard_handle_check(handle))
		return SCARD_E_INVALID_HANDLE;

	bool auto_alloc = false;
	wchar_t *name = NULL;

	if (pcchReaderLen != NULL)
	{
		if (szReaderName == NULL)
			return SCARD_E_INVALID_PARAMETER;

		if (*pcchReaderLen == SCARD_AUTOALLOCATE)
		{
			name = memAlloc(_scard_READER_NAME_SIZE_W * sizeof(wchar_t));
			if (name == NULL) {
				*((LPWSTR *)szReaderName) = NULL;
				return SCARD_E_NO_MEMORY;
			}

			*((LPWSTR *)szReaderName) = name;
			auto_alloc = true;
		}
		else {
			if (*pcchReaderLen < _scard_READER_NAME_SIZE_W)
				return SCARD_E_INSUFFICIENT_BUFFER;

			name = szReaderName;
		}

		swprintf_s(name, _scard_READER_NAME_SIZE_W, L"%s %d", readerNameW, ((struct itecard_handle *)hCard)->id);
		*pcchReaderLen = wstrLen(name) + 2;

		name[*pcchReaderLen - 1] = L'\0';
	}

	LONG r;

	r = _scardStatus(handle, pdwState, pdwProtocol, pbAtr, pcbAtrLen);
	if (r != SCARD_S_SUCCESS && auto_alloc == true) {
		memFree(name);
		*((LPWSTR*)szReaderName) = NULL;
	}

	return r;
}

LONG WINAPI SCardTransmit(SCARDHANDLE hCard, LPCSCARD_IO_REQUEST pioSendPci, LPCBYTE pbSendBuffer, DWORD cbSendLength, LPSCARD_IO_REQUEST pioRecvPci, LPBYTE pbRecvBuffer, LPDWORD pcbRecvLength)
{
	dbg(L"SCardTransmit(ITE)");

	struct itecard_handle *handle = (struct itecard_handle *)hCard;

	if (!itecard_handle_check(handle))
		return SCARD_E_INVALID_HANDLE;

	if (pioSendPci == NULL || pbSendBuffer == NULL || pbRecvBuffer == NULL || pcbRecvLength == NULL || *pcbRecvLength == SCARD_AUTOALLOCATE)
		return SCARD_E_INVALID_PARAMETER;

	itecard_protocol_t protocol;

	switch (pioSendPci->dwProtocol)
	{
	case SCARD_PROTOCOL_T1:
		if (handle->dev->card.T1.b == false) {
			return SCARD_E_UNSUPPORTED_FEATURE;
		}
		protocol = ITECARD_PROTOCOL_T1;
		break;

	default:
		return SCARD_E_READER_UNSUPPORTED;
	}

	return itecard_status_to_scard_status(itecard_transmit(handle, protocol, pbSendBuffer, cbSendLength, pbRecvBuffer, pcbRecvLength));
}
