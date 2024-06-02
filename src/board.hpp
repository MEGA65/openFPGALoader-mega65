// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (C) 2019 Gwenhael Goavec-Merou <gwenhael.goavec-merou@trabucayre.com>
 */

#ifndef BOARD_HPP
#define BOARD_HPP

#include <map>

#include "cable.hpp"

/* AN_232R-01_Bit_Bang_Mode_Available_For_FT232R_and_Ft245R */
enum {
	FT232RL_TXD = 0,
	FT232RL_RXD = 1,
	FT232RL_RTS = 2,
	FT232RL_CTS = 3,
	FT232RL_DTR = 4,
	FT232RL_DSR = 5,
	FT232RL_DCD = 6,
	FT232RL_RI  = 7
};

/* AN_108_Command_Processor_for_MPSSE_and_MCU_Host_Bus_Emulation_Modes */
enum {
	DBUS0 = (1 <<  0),
	DBUS1 = (1 <<  1),
	DBUS2 = (1 <<  2),
	DBUS3 = (1 <<  3),
	DBUS4 = (1 <<  4),
	DBUS5 = (1 <<  5),
	DBUS6 = (1 <<  6),
	DBUS7 = (1 <<  7),
	CBUS0 = (1 <<  8),
	CBUS1 = (1 <<  9),
	CBUS2 = (1 << 10),
	CBUS3 = (1 << 11),
	CBUS4 = (1 << 12),
	CBUS5 = (1 << 13),
	CBUS6 = (1 << 14),
	CBUS7 = (1 << 15)
};

/*!
 * \brief for bitbang mode this structure provide value for each JTAG signals
 */
typedef struct {
	uint8_t tms_pin; /*! TMS pin value */
	uint8_t tck_pin; /*! TCK pin value */
	uint8_t tdi_pin; /*! TDI pin value */
	uint8_t tdo_pin; /*! TDO pin value */
} jtag_pins_conf_t;

typedef struct {
	uint16_t cs_pin;    /*! CS pin value */
	uint16_t sck_pin;   /*! SCK pin value */
	uint16_t miso_pin;  /*! MISO pin value */
	uint16_t mosi_pin;  /*! MOSI pin value */
	uint16_t holdn_pin; /*! HOLDN pin value */
	uint16_t wpn_pin;   /*! WPN pin value */
} spi_pins_conf_t;

enum {
	COMM_JTAG = (1 << 0),
	COMM_SPI  = (1 << 1),
	COMM_DFU  = (1 << 2),
};

/*!
 * \brief a board has a target cable and optionally a pin configuration
 * (bitbang mode)
 */
typedef struct {
	std::string manufacturer;
	std::string cable_name; /*! provide name of one entry in cable_list */
	std::string fpga_part;  /*! provide full fpga model name with package */
	uint16_t reset_pin;      /*! reset pin value */
	uint16_t done_pin;       /*! done pin value */
	uint16_t oe_pin;         /*! output enable pin value */
	uint16_t mode;           /*! communication type (JTAG or SPI) */
	jtag_pins_conf_t jtag_pins_config; /*! for bitbang, provide struct with pins value */
	spi_pins_conf_t spi_pins_config; /*! for SPI, provide struct with pins value */
	uint32_t default_freq;   /* Default clock speed: 0 = use cable default */
	uint16_t vid;             /* optional VID: used only with DFU */
	uint16_t pid;             /* optional VID: used only with DFU */
	int16_t altsetting;       /* optional altsetting: used only with DFU */
} target_board_t;

#define CABLE_DEFAULT 0
#define CABLE_MHZ(_m) ((_m) * 1000000)

#define JTAG_BOARD(_name, _fpga_part, _cable, _rst, _done, _freq) \
	{_name, {"", _cable, _fpga_part, _rst, _done, 0, COMM_JTAG, {}, {}, _freq, 0, 0, -1}}
#define JTAG_BITBANG_BOARD(_name, _fpga_part, _cable, _rst, _done, _tms, _tck, _tdi, _tdo, _freq) \
	{_name, {"", _cable, _fpga_part, _rst, _done, 0, COMM_JTAG, { _tms, _tck, _tdi, _tdo }, {}, \
	_freq, 0, 0, -1}}
#define SPI_BOARD(_name, _manufacturer, _cable, _rst, _done, _oe, _cs, _sck, _si, _so, _holdn, _wpn, _freq) \
	{_name, {_manufacturer, _cable, "", _rst, _done, _oe, COMM_SPI, {}, \
		{_cs, _sck, _so, _si, _holdn, _wpn}, _freq, 0, 0, -1}}
#define DFU_BOARD(_name, _fpga_part, _cable, _vid, _pid, _alt) \
	{_name, {"", _cable, _fpga_part, 0, 0, 0, COMM_DFU, {}, {}, 0, _vid, _pid, _alt}}

static std::map <std::string, target_board_t> board_list = {
	SPI_BOARD("ice40_generic",    "lattice", "ft2232",
                       DBUS7, DBUS6, 0,
                       DBUS4, DBUS0, DBUS1, DBUS2,
                       0, 0, CABLE_DEFAULT)
};

#endif
