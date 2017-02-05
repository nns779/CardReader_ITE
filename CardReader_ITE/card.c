// card.c

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "debug.h"
#include "card.h"

bool card_init(struct card_info *const card)
{
	memset(card, 0, sizeof(struct card_info));

	card->etu = 372 / 4;
	card->f = 4;
	card->Fi = 372;
	card->Di = 1;

	card->T0.WI = 10;

	card->T1.IFSC = 32;
	card->T1.IFSD = 32;
	card->T1.CWI = 13;
	card->T1.BWI = 4;
	card->T1.EDC = 0;

	return true;
}

bool card_clear(struct card_info *const card)
{
	memset(card, 0, sizeof(struct card_info));
	return true;
}

static bool _card_TA(struct card_info *const card, const int i, const int t, const uint8_t v)
{
	dbg(L"_card_TA: i: %d, t: %d, v: %d", i, t, v);

	switch (i)
	{
	case 1:
		switch (v & 0xf0)
		{
		case 0x00:
			card->Fi = 372;
			card->f = 4;
			break;

		case 0x10:
			card->Fi = 372;
			card->f = 5;
			break;

		case 0x20:
			card->Fi = 558;
			card->f = 6;
			break;

		case 0x30:
			card->Fi = 744;
			card->f = 8;
			break;

		case 0x40:
			card->Fi = 1116;
			card->f = 12;
			break;

		case 0x50:
			card->Fi = 1488;
			card->f = 16;
			break;

		case 0x60:
			card->Fi = 1860;
			card->f = 20;
			break;

		case 0x90:
			card->Fi = 512;
			card->f = 5;
			break;

		case 0xA0:
			card->Fi = 768;
			card->f = 7;	// 7.5
			break;

		case 0xB0:
			card->Fi = 1024;
			card->f = 10;
			break;

		case 0xC0:
			card->Fi = 1536;
			card->f = 15;
			break;

		case 0xD0:
			card->Fi = 2048;
			card->f = 20;
			break;

		default:
			internal_err(L"_card_TA(1): not supported card 1");
			return false;
		}

		switch (v & 0x0f) {
		case 0x01:
			card->Di = 1;
			break;

		case 0x02:
			card->Di = 2;
			break;

		case 0x03:
			card->Di = 4;
			break;

		case 0x04:
			card->Di = 8;
			break;

		case 0x05:
			card->Di = 16;
			break;

		case 0x06:
			card->Di = 32;
			break;

		case 0x07:
			card->Di = 64;
			break;

		case 0x08:
			card->Di = 12;
			break;

		case 0x09:
			card->Di = 20;
			break;

		default:
			internal_err(L"_card_TA(1): not supported card 2");
			return false;
		}

		break;

	case 2:
		switch ((v & 0x0f)) {
		case 1:
			card->T1.b = true;
			break;

		default:
			internal_err(L"_card_TA(2): not supported card");
			return false;
		}
		break;

	default:
		if (t == 1) {
			if (v != 0) {
				internal_err(L"_card_TA(i): not supported card");
			}
			card->T1.IFSC = v;
		}
		break;
	}

	return true;
}

static bool _card_TB(struct card_info *const card, const int i, const int t, const uint8_t v)
{
	dbg(L"_card_TB: i: %d, t: %d, v: %d", i, t, v);

	switch (i)
	{
	case 1:
	case 2:
		// Deprecated
		break;

	default:
		if (t == 1) {
			card->T1.BWI = (v >> 4) & 0x0f;
			card->T1.CWI = (v) & 0x0f;
			dbg(L"_card_TB: BWI: %d, CWI: %d", card->T1.BWI, card->T1.CWI);
		}
		break;
	}

	return true;
}

static bool _card_TC(struct card_info *const card, const int i, const int t, const uint8_t v)
{
	switch (i)
	{
	case 1:
		card->N = v;
		break;
		
	case 2:
		card->T0.WI = v;
		break;

	default:
		if (t == 1) {
			card->T1.EDC = v;
		}
		break;
	}

	return true;
}

bool card_parseATR(struct card_info *const card)
{
	uint8_t *atr = card->atr;
	uint8_t atr_len = card->atr_len;

	dbg(L"ATR: length: %d", atr_len);

	// TS
	if (atr[0] != 0x3B) {
		internal_err(L"card_parseATR: not supported card (TS)");
		return false;
	}

	uint8_t hb_len;		// length of historical byte
	uint8_t t_len;
	uint8_t t;

	// T0
	hb_len = (atr[1] & 0x0f);
	t = atr[1] & 0xf0;

	if (t == 0) {
		//card->T0.b = true;
		return false;
	}
	else {
		t_len = atr_len - 2/*TS,T0*/ - hb_len - 1/*TCK*/;
	}

	uint8_t idx = 2, ti = 1;
	int protocol = -1;

	while (t_len)
	{
		dbg(L"card_parseATR: idx: %d, ti: %d, t: %x", idx, ti, t);

		// TA
		if (t & 0x10) {
			if (_card_TA(card, ti, protocol, atr[idx]) == false) {
				internal_err(L"card_parseATR: not supported card (TA)");
				return false;
			}
			idx++;
			t_len--;
		}

		// TB
		if (t & 0x20) {
			if (t_len == 0) {
				internal_err(L"card_parseATR: no enough size (TB)");
				return false;
			}
			else if (_card_TB(card, ti, protocol, atr[idx]) == false) {
				internal_err(L"card_parseATR: not supported card (TB)");
				return false;
			}
			idx++;
			t_len--;
		}

		// TC
		if (t & 0x40) {
			if (t_len == 0) {
				internal_err(L"card_parseATR: no enough size (TC)");
				return false;
			}
			else if (_card_TC(card, ti, protocol, atr[idx]) == false) {
				internal_err(L"card_parseATR: not supported card (TC)");
				return false;
			}
			idx++;
			t_len--;
		}

		// TD
		if (t & 0x80) {
			if (t_len == 0) {
				internal_err(L"card_parseATR: no enough size (TD)");
				return false;
			}

			t = atr[idx] & 0xf0;
			protocol = atr[idx] & 0x0f;
			ti++;
			idx++;
			t_len--;

			if (t == 0) {
				break;
			}
		}
		else {
			break;
		}
	}

	if (t_len) {
		internal_err(L"card_parseATR: invalid ATR");
		return false;
	}

	// error detection
	uint8_t c = 0;
	for (uint8_t i = 1; i < atr_len; i++)
		c ^= atr[i];

	if (c != 0) {
		internal_err(L"card_parseATR: TCK error");
		return false;
	}

	card->etu = (card->Fi / (card->Di * card->f));
	dbg(L"card_parseATR: etu: %d", card->etu);

	if (card->T0.b == true) {
		card->T0.WT = (card->T0.WI * 960 * card->Di);
	}

	if (card->T1.b == true) {
		card->T1.BWT = (2 * card->T1.BWI * 960 * 372 / card->f) + (11 * card->etu);
		card->T1.CWT = (2 * card->T1.CWI + 11) * card->etu;
		card->T1.BGT = 22 * card->etu;
		dbg(L"card_parseATR: BWT: %d, CWT: %d, BGT: %d", card->T1.BWT, card->T1.CWT, card->T1.BGT);
	}

	memcpy(card->atr, atr, atr_len);
	card->atr_len = atr_len;

	return true;
}

int card_T1MakeBlock(struct card_info *const card, uint8_t *const p, const uint8_t code, const uint8_t *const inf, const uint8_t inf_len)
{
	int r;

	dbg(L"card_T1MakeBlock: code: %02X, inf_len: %d", code, inf_len);

	p[0] = 0;
	
	switch (code & 0xC0) {
	case 0x00:
		// I
		dbg(L"card_T1MakeBlock: I");
		p[1] = (card->T1.seq & 0x01) << 6;
		r = 0;
		break;

	case 0x80:
		// R
		dbg(L"card_T1MakeBlock: R");
		p[1] = (code & 0xAF) | ((card->T1.seq & 0x01) << 4);
		r = 1;
		break;

	case 0xC0:
		// S
		dbg(L"card_T1MakeBlock: S");
		p[1] = (code & 0xE3);
		r = 2;
		break;

	default:
		return -1;
	}

	if (inf != NULL) {
		p[2] = inf_len;
		memcpy(p + 3, inf, inf_len);
	}
	else {
		p[2] = 0;
	}

	uint8_t edc = 0;

	for (uint32_t i = 0; i < (3UL + p[2]); i++)
		edc ^= p[i];

	// add EDC
	p[3 + p[2]] = edc;

	return r;
}

bool card_T1CheckBlockEDC(struct card_info *const card, const uint8_t *const p, const uint32_t len)
{
	uint8_t *b = (uint8_t *)p, edc = 0;
	uint32_t l = len;
	
	while (l) {
		edc ^= *b++;
		l--;
	}

	return (edc) ? false : true;
}