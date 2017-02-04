// card.h

#pragma once

#include <stdbool.h>
#include <stdint.h>

#pragma pack(4)

struct card_info
{
	uint8_t atr[64];
	uint8_t atr_len;

	uint8_t reserved1;
	uint32_t etu;	// F/(Di*f) (microseconds)
	uint8_t f;		// TA1 (MHz)
	uint16_t Fi;	// TA1
	uint8_t Di;		// TA1
	uint8_t P;		// TB1/TB2 (not used)
	uint8_t II;		// TB1 (not used)
	uint8_t N;		// TC1 (reserved)
	uint32_t GT;	// TC1 (reserved)

	struct {
		bool b;
	} T0;

	struct {
		bool b;
		uint8_t seq;
		uint8_t IFSC;
		uint8_t IFSD;
		uint8_t CWI;
		uint8_t BWI;
		uint8_t EDC;	// (not used)
		uint32_t CWT;
		uint32_t BWT;
		uint32_t BGT;
	} T1;
};

#pragma pack()

extern bool card_init(struct card_info *const card);
extern bool card_clear(struct card_info *const card);
extern bool card_parseATR(struct card_info *const card);
extern int card_T1MakeBlock(struct card_info *const card, uint8_t *const p, const uint8_t code, const uint8_t *const inf, const uint8_t inf_len);
extern bool card_T1CheckBlockEDC(struct card_info *const card, const uint8_t *const p, const uint32_t len);
