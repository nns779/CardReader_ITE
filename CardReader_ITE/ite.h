// ite.h

#pragma once

#include <stdint.h>
#include <windows.h>

typedef struct _ite_dev {
	HANDLE dev;
} ite_dev;

#pragma pack(2)

/*
	KSPROPSETID_IteDevice ({c6efe5eb-855a-4f1b-b7aa-87b5e1dc4113})

	PropId: 5 (MercuryDeviceInfo) (get)
*/

struct ite_mercury_device_info
{
	uint16_t mode;
	uint16_t vid;
	uint16_t pid;
};

#pragma pack()

#pragma pack(4)

/*
	KSPROPSETID_IteDeviceControl ({f23fac2d-e1af-48e0-8bbe-a14029c92f11})

	PropId: 0 (IOCTL for driver)
	  0x01: Get Driver Information

	PropId: 1 (IOCTL)
	  0x00: Read OFDM Register
	  0x01: Write OFDM Register
	  0x02: Read Link Register
	  0x03: Write Link Register
	  0x04: Ap Control ?
	  0x07: Read Raw IR
	  0x0A: Get UART Data
	  0x0B: Send UART Data
	  0x0C: Set UART Baudrate
	  0x0D: Detect Card
	  0x0E: Get ATR
	  0x0F: Set AES Key
	  0x10: Enable AES
	  0x11: Reset Smartcard
	  0x12: Check Ready (BCAS)
	  0x13: Set 1-Seg
	  0x19: Get Board Input Power ?
	  0x1A: Set Decrypt
	  0x64: Get Rx Device ID
	  0x65: Stop Checking Return Channel PID (Tx) ?
	  0x12C: Read EEPROM (IT930x)
	  0x12D: Write EEPROM (IT930x)
	  0x12E: Read GPIO
	  0x12F: Write GPIO
	
	PropId: 0xC8 (Set TSID)
	  word ptr
*/

struct ite_driver_data
{
	uint32_t pid;
	uint32_t version;
	uint32_t fw_version_link;
	uint32_t fw_version_ofdm;
	uint8_t tuner_id;
};

#define ITE_DEVCTL_READ_OFDM_REG			0x00
#define ITE_DEVCTL_WRITE_OFDM_REG			0x01
#define ITE_DEVCTL_READ_LINK_REG			0x02
#define ITE_DEVCTL_WRITE_LINK_REG			0x03
#define ITE_DEVCTL_AP_CONTROL				0x04
#define ITE_DEVCTL_READ_RAW_IR				0x07
#define ITE_DEVCTL_UART_RECV_DATA			0x0A
#define ITE_DEVCTL_UART_SEND_DATA			0x0B
#define ITE_DEVCTL_UART_SET_BAUDRATE		0x0C
#define ITE_DEVCTL_CARD_DETECT				0x0D
#define ITE_DEVCTL_CARD_GET_ATR				0x0E
#define ITE_DEVCTL_AES_SET_KEY				0x0F
#define ITE_DEVCTL_AES_ENABLE				0x10
#define ITE_DEVCTL_CARD_RESET				0x11
#define ITE_DEVCTL_UART_CHECK_READY			0x12
#define ITE_DEVCTL_SET_ONESEG				0x13
#define ITE_DEVCTL_GET_BOARD_INPUT_POWER	0x19
#define ITE_DEVCTL_SET_DECRYPT				0x1A

struct ite_devctl_data
{
	union {
		uint32_t code;			// Device control code
		uint32_t ui32_rval;		// 32bit return value
		uint8_t ui8_rval;		// 8bit return value (reg val)
	};

	union {
		uint32_t ui32_val;

		// OFDM/Link Register
		struct {
			uint32_t addr;		// register address
			uint8_t val;		// for write
		} reg;

		// UART buffer
		struct {
			uint8_t length;
			uint8_t buffer[0x103];
		} uart_data;

		// ?
		struct {
			uint32_t key;
			uint8_t enable;
		} decrypt;

		// EEPROM
		struct {
			uint8_t index;
			uint8_t val;		// for write
		} eeprom;
	};

	// 108h
	union {
		uint32_t uart_ready;
		uint8_t card_present;
		uint32_t aes_enable;
	};

	uint16_t reserved1;

	uint16_t uart_baudrate;		// 10Eh
	uint8_t card_atr[0x0d];		// 110h
	uint8_t aes_key[0x10];		// 11Dh
};

/*
	KSPROPSETID_IteSatControl ({f23fac2d-e1af-48e0-8bbe-a14029c92f21})

	PropId: 0 (LNB Power)

	PropId: 1 (DVB-S DiSEqC)
*/

struct ite_lnb_data {
	uint32_t power;		// on: 1, off: 0
	uint32_t reserved1;
	uint8_t reserved2;
	uint32_t reserved3;
};

#pragma pack()

typedef enum _ite_property_op {
	ITE_PROPERTY_GET,
	ITE_PROPERTY_SET,
} ite_property_op;

typedef enum _ite_ioctl_type {
	ITE_IOCTL_IN,	// from device
	ITE_IOCTL_OUT,	// to device
} ite_ioctl_type;

extern bool ite_open(ite_dev *const dev, const wchar_t *const path);
extern bool ite_close(ite_dev *const dev);
extern bool ite_dev_ioctl(ite_dev *const dev, const uint32_t code, const ite_ioctl_type type, const void *const in, const uint32_t in_size, const void *const out, const uint32_t out_size);
extern bool ite_devctl(ite_dev *const dev, const ite_ioctl_type type, struct ite_devctl_data *const data);
extern bool ite_sat_ioctl(ite_dev *const dev, const uint32_t code, const ite_ioctl_type type, const void *const data, const uint32_t data_size);
