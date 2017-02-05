// itecard.h

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <windows.h>

#include "card.h"
#include "ite.h"

#define ITECARD_MAX_DEV_NUM	8
#define ITECARD_MAX_DEV_REF_COUNT	24

typedef enum _itecard_protocol_t
{
	ITECARD_PROTOCOL_UNDEFINED = 0,
	ITECARD_PROTOCOL_T0 = 1,
	ITECARD_PROTOCOL_T1 = 2
} itecard_protocol_t;

// shared data

#pragma pack(4)

struct itecard_db_dev
{
	wchar_t path[512];
	uint32_t ref;
	bool exclusive;
	bool reset;
	struct card_info card;
};

struct itecard_db
{
	uint32_t signature;
	uint32_t count;
	uint32_t size;
	struct itecard_db_dev dev[ITECARD_MAX_DEV_NUM];
};

#pragma pack()

// local

struct itecard_db_handle
{
	HANDLE mutex;
	HANDLE shmem;
	struct itecard_db *card;	// ptr to shared data
};

struct itecard_handle
{
	uint32_t signature;			// 0x87E429BB
	struct itecard_db_handle db;
	uint32_t id;
	bool exclusive;
	itecard_protocol_t protocol;
	struct itecard_db_dev *dev;
	ite_dev ite;
};

struct itecard_devlist
{
	uint32_t signature;			// 0xB54F1E9A
	struct itecard_db_handle db;
	wchar_t name[128];
};

typedef enum _itecard_status
{
	ITECARD_S_OK,
	ITECARD_S_FALSE,
	ITECARD_E_FAILED,
	ITECARD_E_FATAL,
	ITECARD_E_INTERNAL,
	ITECARD_E_UNSUPPORTED,
	ITECARD_E_INVALID_PARAMETER,
	ITECARD_E_INSUFFICIENT_BUFFER,
	ITECARD_E_NO_MEMORY,
	ITECARD_E_NO_DEVICE,
	ITECARD_E_NO_CARD,
	ITECARD_E_NO_DATA,
	ITECARD_E_NO_MORE_DATA,
	ITECARD_E_INTERNAL_LIMIT,
	ITECARD_E_NOT_READY,
	ITECARD_E_NOT_SHARED,
	ITECARD_E_SHARED,
	ITECARD_E_UNRESPONSIVE_CARD,
	ITECARD_E_UNSUPPORTED_CARD,
	ITECARD_E_PROTO_MISMATCH,
	ITECARD_E_TOO_LARGE,
	ITECARD_E_COMM_FAILED,
} itecard_status_t;

#define ITECARD_DEVLIST_SIGNATURE	0xB54F1E9A

#define itecard_devlist_check_signature(dl) ((dl)->signature == ITECARD_DEVLIST_SIGNATURE)
#define itecard_devlist_check(dl) ((dl) != NULL && (dl)->signature == ITECARD_DEVLIST_SIGNATURE)

typedef int(*itecard_devlist_enum_callback)(struct itecard_devlist *const devlist, const uint32_t id, const wchar_t *const path, void *prm);

extern itecard_status_t itecard_devlist_open(struct itecard_devlist *const devlist, const wchar_t *const name);
extern itecard_status_t itecard_devlist_close(struct itecard_devlist *const devlist);
extern itecard_status_t itecard_devlist_enum(struct itecard_devlist *const devlist, const itecard_devlist_enum_callback callback, void *prm);
extern itecard_status_t itecard_devlist_register(struct itecard_devlist *const devlist, const uint32_t id);

#define ITECARD_SIGNATURE	0x87E429BB

#define itecard_handle_check_signature(handle) ((handle)->signature == ITECARD_SIGNATURE)
#define itecard_handle_check(handle) ((handle) != NULL && (handle)->signature == ITECARD_SIGNATURE)

extern itecard_status_t itecard_open(struct itecard_handle *const handle, const wchar_t *const name, const uint32_t id, const itecard_protocol_t protocol, const bool exclusive/*, itecard_protocol_t *const active_protocol*/);
extern itecard_status_t itecard_close(struct itecard_handle *const handle, const bool reset);
extern itecard_status_t itecard_detect(struct itecard_handle *const handle, bool *const b);
extern itecard_status_t itecard_init(struct itecard_handle *const handle);
extern itecard_status_t itecard_transmit(struct itecard_handle *const handle, const itecard_protocol_t protocol, const uint8_t *const sendBuf, const uint32_t sendLen, uint8_t *const recvBuf, uint32_t *const recvLen);
