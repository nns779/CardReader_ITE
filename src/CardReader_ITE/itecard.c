// itecard.c

#include <stdbool.h>
#include <stdint.h>
#include <windows.h>

#include "debug.h"
#include "memory.h"
#include "itecard.h"
#include "ite.h"

#define micro2milli(microseconds) (((microseconds) / 1000) + 1)

itecard_status_t itecard_open(struct itecard_handle *const handle, const wchar_t *const path, struct itecard_shared_readerinfo *const reader, const itecard_protocol_t protocol, const bool exclusive, const bool power_on)
{
	itecard_status_t r = ITECARD_E_INTERNAL;

	if (handle->init == true)
		return ITECARD_S_OK;

	if (protocol != ITECARD_PROTOCOL_UNDEFINED)
	{
		if (reader->exclusive == 1) {
			internal_err("itecard_open: not allowed");
			r = ITECARD_E_NOT_SHARED;
			goto end1;
		}
	}

	ite_dev *ite = &handle->ite;

	// open device
	if (ite_open(ite, path) == false) {
		internal_err("itecard_open: ite_open failed");
		r = ITECARD_E_NO_DEVICE;
		goto end2;
	}

	// set power
	if (power_on != false) {
		if (ite_v_supported_private_ioctl(ite) == true) {
			dbg("itecard_open: private ioctl is supported");
			ite_private_ioctl(ite, ITE_IOCTL_OUT, 1);
			ite_private_ioctl(ite, ITE_IOCTL_OUT, 2);
			ite_private_ioctl(ite, ITE_IOCTL_OUT, 0);
			dbg("itecard_open: power on");
		}
		else {
			dbg("itecard_open: private ioctl is not supported");
		}
	}

	if (exclusive == true) {
		reader->exclusive = 1;
	}

	handle->init = true;
	handle->exclusive = exclusive;
	handle->protocol = protocol;
	handle->reader = reader;

	return ITECARD_S_OK;

end2:
	ite_close(ite);
end1:
	return r;
}

itecard_status_t itecard_close(struct itecard_handle *const handle, const bool reset, const bool noref, const bool power_off)
{
	if (handle->init == false)
		return ITECARD_S_OK;

	if (noref == true)
	{
		ite_dev *ite = &handle->ite;

		if (power_off != false) {
			if (ite_v_supported_private_ioctl(ite) == true) {
				dbg("itecard_close: private ioctl is supported");
				ite_private_ioctl(ite, ITE_IOCTL_OUT, 1);
				ite_private_ioctl(ite, ITE_IOCTL_OUT, 3);
				ite_private_ioctl(ite, ITE_IOCTL_OUT, 0);
				dbg("itecard_close: power off");
			}
			else {
				dbg("itecard_close: private ioctl is not supported");
			}
		}
	}

	ite_close(&handle->ite);

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

static itecard_status_t _itecard_detect(struct itecard_handle *const handle, bool *const b)
{
	struct ite_devctl_data d;

	d.code = ITE_DEVCTL_CARD_DETECT;

	if (ite_devctl(&handle->ite, ITE_IOCTL_IN, &d) == false) {
		internal_err("_itecard_detect: ite_devctl failed");
		return ITECARD_E_FAILED;
	}

	*b = (d.card_present == 0) ? false : true;

	return ITECARD_S_OK;
}

static itecard_status_t _itecard_reset(struct itecard_handle *const handle)
{
	struct ite_devctl_data d;

	d.code = ITE_DEVCTL_CARD_RESET;

	if (ite_devctl(&handle->ite, ITE_IOCTL_OUT, &d) == false) {
		internal_err("_itecard_reset: ite_devctl failed");
		return ITECARD_E_FAILED;
	}

	return ITECARD_S_OK;
}

static itecard_status_t _itecard_set_baudrate(struct itecard_handle *const handle, const uint16_t baudrate)
{
	struct ite_devctl_data d;

	d.code = ITE_DEVCTL_UART_SET_BAUDRATE;
	d.uart_baudrate = baudrate;

	if (ite_devctl(&handle->ite, ITE_IOCTL_OUT, &d) == false) {
		internal_err("_itecard_set_baudrate: ite_devctl failed");
		return ITECARD_E_FAILED;
	}

	return ITECARD_S_OK;
}

static itecard_status_t _itecard_send(struct itecard_handle *const handle, const uint8_t *const sendBuf, const uint8_t sendLen)
{
	struct ite_devctl_data d;

	d.code = ITE_DEVCTL_UART_SEND_DATA;
	memcpy(d.uart_data.buffer, sendBuf, sendLen);
	d.uart_data.length = sendLen;

	if (ite_devctl(&handle->ite, ITE_IOCTL_OUT, &d) == false) {
		internal_err("_itecard_send: ite_devctl failed");
		return ITECARD_E_FAILED;
	}

	return ITECARD_S_OK;
}

static itecard_status_t _itecard_recv(struct itecard_handle *const handle, uint8_t *const recvBuf, uint8_t *const recvLen)
{
	struct ite_devctl_data d;

	d.code = ITE_DEVCTL_UART_CHECK_READY;

	if (ite_devctl(&handle->ite, ITE_IOCTL_IN, &d) == false) {
		internal_err("_itecard_recv: ite_devctl failed 1");
		return ITECARD_E_FAILED;
	}
	else if (d.uart_ready == 0) {
		return ITECARD_E_NO_DATA;
	}

	d.code = ITE_DEVCTL_UART_RECV_DATA;
	d.uart_data.length = *recvLen;

	if (ite_devctl(&handle->ite, ITE_IOCTL_IN, &d) == false) {
		internal_err("_itecard_recv: ite_devctl failed 2");
		return ITECARD_E_FAILED;
	}
	else if (d.uart_data.length == 0) {
		return ITECARD_E_NO_DATA;
	}

	memcpy(recvBuf, d.uart_data.buffer, d.uart_data.length);
	*recvLen = d.uart_data.length;

	return ITECARD_S_OK;
}

static itecard_status_t _itecard_get_atr(struct itecard_handle *const handle)
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

		if (_itecard_recv(handle, atr + atr_len, &rl) == ITECARD_S_OK) {
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

static itecard_status_t _itecard_t1_transceive(struct itecard_handle *const handle, const uint8_t *const sendBuf, const uint32_t sendLen, uint8_t *const recvBuf, uint32_t *const recvLen)
{
	itecard_status_t ret;
	struct card_info *card = &handle->reader->card;

	{
		uint8_t pos = 0;

		while (pos < sendLen)
		{
			uint8_t send_len = 0;

			send_len = ((sendLen - pos) > 255) ? 255 : sendLen - pos;

			ret = _itecard_send(handle, sendBuf + pos, send_len);
			if (ret != ITECARD_S_OK) {
				internal_err("_itecard_t1_transceive: _itecard_send failed");
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
	st = micro2milli(card->etu * 96);
	ct = 0;
	cl = 0;

	while (ct <= wt)
	{
		uint8_t rl = ((*recvLen - cl) > 255) ? 255 : ((*recvLen - cl));

		if (rl == 0)
			break;

		ret = _itecard_recv(handle, recvBuf + cl, &rl);
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

static itecard_status_t _itecard_t1_transmit(struct itecard_handle *const handle, const uint8_t code, const uint8_t *const req, const uint32_t req_len, uint8_t *const res, uint32_t *const res_len)
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
			internal_err("_itecard_t1_transmit: card_T1MakeBlock failed");
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

			ret = _itecard_t1_transmit(handle, 0xC1, &ifsd, sizeof(ifsd), res, &res_len);
			if (ret != ITECARD_S_OK) {
				dbg("_itecard_t1_transmit: IFS request failed");
				r = ret;
				break;
			}
			else if (res[0] != 254) {
				dbg("_itecard_t1_transmit: IFSD != 254");
				r = ITECARD_E_FAILED;
				break;
			}

			card->T1.IFSD = 254;
		}

		uint8_t recv_block[259];
		uint32_t recv_len = 259;

		ret = _itecard_t1_transceive(handle, p, 4 + p[2], recv_block, &recv_len);
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
					dbg("_itecard_t1_transmit: R-Block");
					rblock = true;
				}
			}
			else {
				internal_err("_itecard_t1_transmit: block error");
			}

			retry_count++;

			if (retry_count > 3) {
				internal_err("_itecard_t1_transmit: retry_count >= 3");
				r = ITECARD_E_COMM_FAILED;
				break;
			}
			else if (retry_count == 3 && code != 0xC0)
			{
				// RESYNCH Request

				uint8_t res[254];
				uint32_t res_len = 254;

				ret = _itecard_t1_transmit(handle, 0xC0, NULL, 0, res, &res_len);
				if (ret != ITECARD_S_OK) {
					dbg("_itecard_t1_transmit: RESYNCH request failed");
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
			internal_err("_itecard_t1_transmit: no data");
			r = ITECARD_E_COMM_FAILED;
			break;
		}
		else {
			internal_err("_itecard_t1_transmit: _itecard_t1_transceive failed");
			r = ret;
			break;
		}
	}

	return r;
}

static itecard_status_t _itecard_init(struct itecard_handle *const handle, const bool force)
{
	itecard_status_t ret;
	bool b = false;
	struct card_info *card = &handle->reader->card;

	// detect
	ret = _itecard_detect(handle, &b);
	if (ret != ITECARD_S_OK) {
		internal_err("_itecard_init: _itecard_detect failed");
		card_clear(card);
		return ret;
	}

	if (b == false) {
		internal_err("_itecard_init: card not found");
		card_clear(card);
		return ITECARD_E_NO_CARD;
	}

	if (handle->reader->card.atr_len != 0 && force == false) {
		return ITECARD_S_FALSE;
	}

	card_clear(card);

	// reset
	ret = _itecard_reset(handle);
	if (ret != ITECARD_S_OK) {
		internal_err("_itecard_init: _itecard_reset failed");
		return ret;
	}

	Sleep(10);

	int i = 3;

	do
	{
		card_init(card);

		// get atr
		ret = _itecard_get_atr(handle);
		if (ret != ITECARD_S_OK) {
			internal_err("_itecard_init: _itecard_get_atr failed (%d)", i);
			continue;
		}

		if (card->atr_len == 0) {
			internal_err("_itecard_init: unresponsive card (%d)", i);
			card_clear(card);
			ret = ITECARD_E_UNRESPONSIVE_CARD;
			continue;
		}

		// parse atr
		if (card_parseATR(card) == false) {
			internal_err("_itecard_init: unsupported card (%d)", i);
			card_clear(card);
			ret = ITECARD_E_UNSUPPORTED_CARD;
			continue;
		}

		break;
	} while (--i);

	if (!i)
		return ret;

	// set baudrate
	ret = _itecard_set_baudrate(handle, 19200);
	if (ret != ITECARD_S_OK) {
		internal_err("_itecard_init: _itecard_set_baudrate failed");
		card_clear(card);
		return ret;
	}

	return ITECARD_S_OK;
}

itecard_status_t itecard_detect(struct itecard_handle *const handle, bool *const b)
{
	return _itecard_detect(handle, b);
}

// init card
itecard_status_t itecard_init(struct itecard_handle *const handle)
{
	return _itecard_init(handle, false);
}

itecard_status_t itecard_transmit(struct itecard_handle *const handle, const itecard_protocol_t protocol, const uint8_t *const sendBuf, const uint32_t sendLen, uint8_t *const recvBuf, uint32_t *const recvLen)
{
	itecard_status_t ret;

	ret = _itecard_init(handle, false);
	if (ret != ITECARD_S_OK && ret != ITECARD_S_FALSE) {
		internal_err("itecard_transmit: _itecard_init failed 1");
		return ret;
	}

	switch (protocol)
	{
	case ITECARD_PROTOCOL_T1:
		ret = _itecard_t1_transmit(handle, 0x00, sendBuf, sendLen, recvBuf, recvLen);
		if (ret == ITECARD_E_COMM_FAILED)
		{
			ret = _itecard_init(handle, true);
			if (ret != ITECARD_S_OK) {
				internal_err("itecard_transmit: _itecard_init failed 2");
				break;
			}
			else {
				ret = _itecard_t1_transmit(handle, 0x00, sendBuf, sendLen, recvBuf, recvLen);
			}
		}

		if (ret != ITECARD_S_OK) {
			internal_err("itecard_transmit: _itecard_t1_transmit failed (%08X)", ret);
		}
		break;

	default:
		ret = ITECARD_E_UNSUPPORTED;
	}

	dbg("itecard_transmit: ret: %d", ret);

	return ret;
}