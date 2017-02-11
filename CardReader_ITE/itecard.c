// itecard.c

#include <stdbool.h>
#include <stdint.h>
#include <windows.h>

#include "debug.h"
#include "memory.h"
#include "string.h"
#include "itecard.h"
#include "ite.h"

#define micro2milli(microseconds) (((microseconds) / 1000) + 1)

itecard_status_t itecard_open(struct itecard_handle *const handle, const wchar_t *const path, struct itecard_shared_readerinfo *const reader, const itecard_protocol_t protocol, const bool exclusive)
{
	itecard_status_t r = ITECARD_E_INTERNAL;

	if (handle->init == true)
		return ITECARD_S_FALSE;

	if (protocol != ITECARD_PROTOCOL_UNDEFINED)
	{
		if (reader->exclusive == 1) {
			internal_err(L"itecard_open: not allowed");
			r = ITECARD_E_NOT_SHARED;
			goto end1;
		}
	}

	ite_dev *ite = &handle->ite;

	// init device instance
	if (ite_init(ite) == false) {
		internal_err(L"itecard_open: ite_init failed");
		goto end1;
	}

	ite_lock(ite);

	// open device
	if (ite_open(ite, path) == false) {
		internal_err(L"itecard_open: ite_open failed");
		r = ITECARD_E_NO_DEVICE;
		goto end2;
	}

	if (exclusive == true) {
		reader->exclusive = 1;
	}

	handle->init = true;
	handle->exclusive = exclusive;
	handle->protocol = protocol;
	handle->reader = reader;

	ite_unlock(ite);

	return ITECARD_S_OK;

end2:
	ite_close(ite);
	ite_unlock(ite);
	ite_release(ite);
end1:
	return r;
}

itecard_status_t itecard_close(struct itecard_handle *const handle, const bool reset, const bool noref)
{
	if (handle->init == false)
		return ITECARD_S_FALSE;

	ite_close(&handle->ite);
	ite_release(&handle->ite);

	if (handle->reader != NULL)
	{
		if (handle->exclusive == true) {
			handle->reader->exclusive = false;
		}

		if (reset == true || handle->reader->reset == 1) {
			if (noref == true) {
				card_clear(&handle->reader->card);
				handle->reader->reset = 0;
			}
			else {
				handle->reader->reset = 1;
			}
		}

		handle->reader = NULL;
	}

	handle->init = false;

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
	struct card_info *card = &handle->reader->card;

	if (card->atr_len != 0)
		return ITECARD_S_FALSE;

	uint8_t atr[64];
	uint8_t atr_len = 0;

	uint32_t wt;	// time limit (in milliseconds)
	uint32_t st;
	uint32_t ct;

	wt = micro2milli(card->etu * 9600);
	st = micro2milli(card->etu * 24);
	ct = 0;

	while (ct <= wt)
	{
		uint8_t rl = 64 - atr_len;

		if (rl == 0)
			break;

		if (_itecard_recv_nolock(handle, atr + atr_len, &rl) == ITECARD_S_OK) {
			atr_len += rl;
			ct = 0;
		}
		else {
			ct += st;
		}

		Sleep(st);
	}

	memcpy(card->atr, atr, atr_len);
	card->atr_len = atr_len;

	return ITECARD_S_OK;
}

static itecard_status_t _itecard_transmit_t1_nolock(struct itecard_handle *const handle, const uint8_t *const sendBuf, const uint32_t sendLen, uint8_t *const recvBuf, uint32_t *const recvLen)
{
	itecard_status_t ret;
	struct card_info *card = &handle->reader->card;

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

	Sleep(micro2milli(card->T1.BGT));

	uint32_t wt;	// time limit (in milliseconds)
	uint32_t st;
	uint32_t ct;
	uint32_t cl;

	wt = micro2milli(card->T1.BWT - card->T1.BGT);
	st = micro2milli(card->etu * 32);
	ct = 0;
	cl = 0;

	while (ct <= wt)
	{
		uint8_t rl = ((*recvLen - cl) > 255) ? 255 : ((*recvLen - cl));

		if (rl == 0)
			break;

		ret = _itecard_recv_nolock(handle, recvBuf + cl, &rl);
		if (ret == ITECARD_S_OK) {
			if (cl == 0) {
				wt = micro2milli(card->T1.CWT);
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
	struct card_info *card = &handle->reader->card;

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

	if (handle->reader->card.atr_len != 0) {
		return ITECARD_S_FALSE;
	}

	// reset
	ret = _itecard_reset_nolock(handle);
	if (ret != ITECARD_S_OK) {
		internal_err(L"_itecard_init_nolock: _itecard_reset_nolock failed");
		return ret;
	}

	Sleep(10);

	struct card_info *card = &handle->reader->card;

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

	ite_lock(&handle->ite);
	ret = _itecard_detect_nolock(handle, b);
	ite_unlock(&handle->ite);

	return ret;
}

// init card
itecard_status_t itecard_init(struct itecard_handle *const handle)
{
	itecard_status_t ret;

	ite_lock(&handle->ite);
	ret = _itecard_init_nolock(handle);
	ite_unlock(&handle->ite);

	return ret;
}

itecard_status_t itecard_transmit(struct itecard_handle *const handle, const itecard_protocol_t protocol, const uint8_t *const sendBuf, const uint32_t sendLen, uint8_t *const recvBuf, uint32_t *const recvLen)
{
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

	return ret;
}