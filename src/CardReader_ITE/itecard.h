// itecard.h

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "card.h"
#include "ite.h"

typedef enum _itecard_protocol_t
{
	ITECARD_PROTOCOL_UNDEFINED = 0,
	ITECARD_PROTOCOL_T0 = 1,
	ITECARD_PROTOCOL_T1 = 2
} itecard_protocol_t;

// shared data

#pragma pack(4)

struct itecard_shared_readerinfo
{
	uint32_t exclusive;
	uint32_t reset;
	struct card_info card;
};

#pragma pack()

// local

struct itecard_handle
{
	bool init;
	bool exclusive;
	itecard_protocol_t protocol;
	struct itecard_shared_readerinfo *reader;
	ite_dev ite;
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

extern itecard_status_t itecard_open(struct itecard_handle *const handle, const wchar_t *const path, struct itecard_shared_readerinfo *const reader, const itecard_protocol_t protocol, const bool exclusive, const bool power_on);
extern itecard_status_t itecard_close(struct itecard_handle *const handle, const bool reset, const bool noref, const bool power_off);
extern itecard_status_t itecard_detect(struct itecard_handle *const handle, bool *const b);
extern itecard_status_t itecard_init(struct itecard_handle *const handle);
extern itecard_status_t itecard_transmit(struct itecard_handle *const handle, const itecard_protocol_t protocol, const uint8_t *const sendBuf, const uint32_t sendLen, uint8_t *const recvBuf, uint32_t *const recvLen);
