// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (C) 2019 Gwenhael Goavec-Merou <gwenhael.goavec-merou@trabucayre.com>
 */

#include <string.h>
#include <unistd.h>

#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <vector>

#include "board.hpp"
#include "cable.hpp"
#include "cxxopts.hpp"
#include "device.hpp"
#include "display.hpp"
#include "ftdispi.hpp"
#include "ice40.hpp"
#include "lattice.hpp"
#include "libusb_ll.hpp"
#include "jtag.hpp"
#include "part.hpp"
#include "spiFlash.hpp"
#include "rawParser.hpp"

#define DEFAULT_FREQ 	6000000

using namespace std;

struct arguments {
	int8_t verbose;
	bool reset, detect, verify, scan_usb;
	unsigned int offset;
	string bit_file;
	string secondary_bit_file;
	string device;
	string cable;
	string ftdi_serial;
	int ftdi_channel;
	int status_pin;
	uint32_t freq;
	bool invert_read_edge;
	string board;
	bool pin_config;
	bool list_cables;
	bool list_boards;
	bool list_fpga;
	Device::prog_type_t prg_type;
	bool is_list_command;
	bool spi;
	bool dfu;
	string file_type;
	string fpga_part;
	string bridge_path;
	string probe_firmware;
	int index_chain;
	unsigned int file_size;
	string target_flash;
	bool external_flash;
	int16_t altsetting;
	uint16_t vid;
	uint16_t pid;
	int16_t cable_index;
	uint8_t bus_addr;
	uint8_t device_addr;
	string ip_adr;
	uint32_t protect_flash;
	bool unprotect_flash;
	bool bulk_erase_flash;
	string flash_sector;
	bool skip_load_bridge;
	bool skip_reset;
	/* xvc server */
	bool xvc;
	int port;
	string interface;
	string mcufw;
	bool conmcu;
	std::map<uint32_t, misc_device> user_misc_devs;
	bool read_dna;
	bool read_xadc;
	string read_register;
};

int parse_opt(int argc, char **argv, struct arguments *args,
	jtag_pins_conf_t *pins_config);

int main(int argc, char **argv)
{
	cable_t cable;
	target_board_t *board = NULL;
	jtag_pins_conf_t pins_config = {0, 0, 0, 0};

	/* command line args. */
	struct arguments args = {0, false, false, false, false, 0, "", "", "", "-", "", -1,
			-1, 0, false, "-", false, false, false, false, Device::PRG_NONE, false,
			/* spi dfu    file_type fpga_part bridge_path probe_firmware */
			false, false, "",       "",       "",         "",
			/* index_chain file_size target_flash external_flash altsetting */
			-1,            0,        "primary",   false,         -1,
			/* vid, pid, index bus_addr, device_addr */
				0,   0,   -1,     0,         0,
			"127.0.0.1", 0, false, false, "", false, false,
			/* xvc server */
			false, 3721, "-",
			"", false, {},  // mcufw conmcu, user_misc_dev_list
			false, false, "" // read_dna, read_xadc, read_register
	};
	/* parse arguments */
	try {
		if (parse_opt(argc, argv, &args, &pins_config))
			return EXIT_SUCCESS;
	} catch (std::exception &e) {
		printError("Error in parse arg step");
		return EXIT_FAILURE;
	}

	if (args.prg_type == Device::WR_SRAM)
		cout << "write to ram" << endl;
	if (args.prg_type == Device::WR_FLASH)
		cout << "write to flash" << endl;

	if (args.board[0] != '-') {
		if (board_list.find(args.board) != board_list.end()) {
			board = &(board_list[args.board]);
		} else {
			printError("Error: cannot find board \'" +args.board + "\'");
			return EXIT_FAILURE;
		}
	}

	/* if a board name is specified try to use this to determine cable */
	if (board) {
		/* set pins config (only when user has not already provided
		 * configuration
		 */
		if (!args.pin_config) {
			pins_config.tdi_pin = board->jtag_pins_config.tdi_pin;
			pins_config.tdo_pin = board->jtag_pins_config.tdo_pin;
			pins_config.tms_pin = board->jtag_pins_config.tms_pin;
			pins_config.tck_pin = board->jtag_pins_config.tck_pin;
		}
		/* search for cable */
		auto t = cable_list.find(board->cable_name);
		if (t != cable_list.end()) {
			if (args.cable[0] == '-') {  // no user selection
				args.cable = (*t).first;  // use board default cable
			} else {
				cout << "Board default cable overridden with " << args.cable << endl;
			}
		}

		/* Xilinx only: to write flash exact fpga model must be provided */
		if (!board->fpga_part.empty() && !args.fpga_part.empty())
			printInfo("Board default fpga part overridden with " + args.fpga_part);
		else if (!board->fpga_part.empty() && args.fpga_part.empty())
			args.fpga_part = board->fpga_part;

		/* Some boards can override the default clock speed
		 * if args.freq == 0: the `--freq` arg has not been used
		 * => apply board->default_freq with a value of
		 * 0 (no default frequency) or > 0 (board has a default frequency)
		 */
		if (args.freq == 0)
			args.freq = board->default_freq;
	}

	if (args.cable[0] == '-') { /* if no board and no cable */
		printWarn("No cable or board specified: using direct ft2232 interface");
		args.cable = "ft2232";
	}

	/* if args.freq == 0: no user requirement nor board default
	 * clock speed => set default frequency
	 */
	if (args.freq == 0)
		args.freq = DEFAULT_FREQ;

	auto select_cable = cable_list.find(args.cable);
	if (select_cable == cable_list.end()) {
		printError("error : " + args.cable + " not found");
		return EXIT_FAILURE;
	}
	cable = select_cable->second;

	if (args.ftdi_channel != -1) {
		if (cable.type != MODE_FTDI_SERIAL && cable.type != MODE_FTDI_BITBANG){
			printError("Error: FTDI channel param is for FTDI cables.");
			return EXIT_FAILURE;
		}

		const int mapping[] = {INTERFACE_A, INTERFACE_B, INTERFACE_C,
			INTERFACE_D};
		cable.config.interface = mapping[args.ftdi_channel];
	}

	if (!args.ftdi_serial.empty()) {
		if (cable.type != MODE_FTDI_SERIAL && cable.type != MODE_FTDI_BITBANG){
			printError("Error: FTDI serial param is for FTDI cables.");
			return EXIT_FAILURE;
		}
	}

	if (args.status_pin != -1) {
		if (cable.type != MODE_FTDI_SERIAL){
			printError("Error: FTDI status pin is for FTDI MPSSE cables.");
			return EXIT_FAILURE;
		}
	}

	if (args.vid != 0) {
		printInfo("Cable VID overridden");
		cable.vid = args.vid;
	}
	if (args.pid != 0) {
		printInfo("Cable PID overridden");
		cable.pid = args.pid;
	}

	cable.bus_addr = args.bus_addr;
	cable.device_addr = args.device_addr;

	// always set these
	cable.config.index = args.cable_index;
	cable.config.status_pin = args.status_pin;

	/* FLASH direct access */
	if (args.spi || (board && board->mode == COMM_SPI)) {
		/* if no instruction from user -> select flash mode */
		if (args.prg_type == Device::PRG_NONE)
			args.prg_type = Device::WR_FLASH;

		FtdiSpi *spi = NULL;
		spi_pins_conf_t pins_config;
		if (board)
			pins_config = board->spi_pins_config;

		try {
			spi = new FtdiSpi(cable, pins_config, args.freq, args.verbose);
		} catch (std::exception &e) {
			printError("Error: Failed to claim cable");
			return EXIT_FAILURE;
		}

		int spi_ret = EXIT_SUCCESS;

		if (board && board->manufacturer != "none") {
			Device *target;
			if (board->manufacturer == "lattice") {
				target = new Ice40(spi, args.bit_file, args.file_type,
					args.prg_type,
					board->reset_pin, board->done_pin, args.verify, args.verbose);
			} else {
				printError("Error (SPI mode): " + board->manufacturer +
					" is an unsupported/unknown target");
				return EXIT_FAILURE;
			}
			if (args.prg_type == Device::RD_FLASH) {
				if (args.file_size == 0) {
					printError("Error: 0 size for dump");
				} else {
					target->dumpFlash(args.offset, args.file_size);
				}
			} else if ((args.prg_type == Device::WR_FLASH ||
						args.prg_type == Device::WR_SRAM) ||
						!args.bit_file.empty() || !args.file_type.empty()) {
				target->program(args.offset, args.unprotect_flash);
			}
			if (args.unprotect_flash && args.bit_file.empty())
				if (!target->unprotect_flash())
					spi_ret = EXIT_FAILURE;
			if (args.bulk_erase_flash && args.bit_file.empty())
				if (!target->bulk_erase_flash())
					spi_ret = EXIT_FAILURE;
			if (args.protect_flash)
				if (!target->protect_flash(args.protect_flash))
					spi_ret = EXIT_FAILURE;
		} else {
			RawParser *bit = NULL;
			if (board && board->reset_pin) {
				spi->gpio_set_output(board->reset_pin, true);
				spi->gpio_clear(board->reset_pin, true);
			}

			SPIFlash flash((SPIInterface *)spi, args.unprotect_flash, args.verbose);
			flash.display_status_reg();

			if (args.prg_type != Device::RD_FLASH &&
					(!args.bit_file.empty() || !args.file_type.empty())) {
				printInfo("Open file " + args.bit_file + " ", false);
				try {
					bit = new RawParser(args.bit_file, false);
					printSuccess("DONE");
				} catch (std::exception &e) {
					printError("FAIL");
					delete spi;
					return EXIT_FAILURE;
				}

				printInfo("Parse file ", false);
				if (bit->parse() == EXIT_FAILURE) {
					printError("FAIL");
					delete spi;
					return EXIT_FAILURE;
				} else {
					printSuccess("DONE");
				}

				try {
					flash.erase_and_prog(args.offset, bit->getData(), bit->getLength()/8);
				} catch (std::exception &e) {
					printError("FAIL: " + string(e.what()));
				}

				if (args.verify)
					flash.verify(args.offset, bit->getData(), bit->getLength() / 8);

				delete bit;
			} else if (args.prg_type == Device::RD_FLASH) {
				flash.dump(args.bit_file, args.offset, args.file_size);
			}

			if (args.unprotect_flash && args.bit_file.empty())
				if (!flash.disable_protection())
					spi_ret = EXIT_FAILURE;
			if (args.bulk_erase_flash && args.bit_file.empty())
				if (!flash.bulk_erase())
					spi_ret = EXIT_FAILURE;
			if (args.protect_flash)
				if (!flash.enable_protection(args.protect_flash))
					spi_ret = EXIT_FAILURE;

			if (board && board->reset_pin)
				spi->gpio_set(board->reset_pin, true);
		}

		delete spi;

		return spi_ret;
	}

	/* jtag base */


	/* if no instruction from user -> select load */
	if (args.prg_type == Device::PRG_NONE)
		args.prg_type = Device::WR_SRAM;

	Jtag *jtag;
	try {
		jtag = new Jtag(cable, &pins_config, args.device, args.ftdi_serial,
				args.freq, args.verbose, args.ip_adr, args.port,
				args.invert_read_edge, args.probe_firmware,
				args.user_misc_devs);
	} catch (std::exception &e) {
		printError("JTAG init failed with: " + string(e.what()));
		return EXIT_FAILURE;
	}

	/* chain detection */
	vector<uint32_t> listDev = jtag->get_devices_list();
	size_t found = listDev.size();
	int idcode = -1;
	size_t index = 0;

	if (args.verbose > 0)
		cout << "found " << std::to_string(found) << " devices" << endl;

	/* in verbose mode or when detect
	 * display full chain with details
	 */
	if (args.verbose > 0 || args.detect) {
		for (size_t i = 0; i < found; i++) {
			uint32_t t = listDev[i];
			printf("index %zu:\n", i);
			if (fpga_list.find(t) != fpga_list.end()) {
				printf("\tidcode 0x%x\n\tmanufacturer %s\n\tfamily %s\n\tmodel  %s\n",
				t,
				fpga_list[t].manufacturer.c_str(),
				fpga_list[t].family.c_str(),
				fpga_list[t].model.c_str());
				printf("\tirlength %d\n", fpga_list[t].irlength);
			} else if (misc_dev_list.find(t) != misc_dev_list.end()) {
				printf("\tidcode   0x%x\n\ttype     %s\n\tirlength %d\n",
				t,
				misc_dev_list[t].name.c_str(),
				misc_dev_list[t].irlength);
			} else if (args.user_misc_devs.find(t) != args.user_misc_devs.end()) {
				printf("\tidcode   0x%x\n\ttype     %s\n\tirlength %d\n",
				t,
				args.user_misc_devs[t].name.c_str(),
				args.user_misc_devs[t].irlength);
			}
		}
		if (args.detect == true) {
			delete jtag;
			return EXIT_SUCCESS;
		}
	}

	if (found != 0) {
		if (args.index_chain == -1) {
			for (size_t i = 0; i < found; i++) {
				if (fpga_list.find(listDev[i]) != fpga_list.end()) {
					index = i;
					if (idcode != -1) {
						printError("Error: more than one FPGA found");
						printError("Use --index-chain to force selection");
						for (size_t i = 0; i < found; i++)
							printf("0x%08x\n", listDev[i]);
						delete(jtag);
						return EXIT_FAILURE;
					} else {
						idcode = listDev[i];
					}
				}
			}
		} else {
			index = args.index_chain;
			if (index > found || index < 0) {
				printError("wrong index for device in JTAG chain");
				delete(jtag);
				return EXIT_FAILURE;
			}
			idcode = listDev[index];
		}
	} else {
		printError("Error: no device found");
		delete(jtag);
		return EXIT_FAILURE;
	}

	jtag->device_select(index);

	/* check if selected device is supported
	 * mainly used in conjunction with --index-chain
	 */
	if (fpga_list.find(idcode) == fpga_list.end()) {
		cerr << "Error: device " << hex << idcode << " not supported" << endl;
		delete(jtag);
		return EXIT_FAILURE;
	}

	string fab = fpga_list[idcode].manufacturer;


	Device *fpga;
	try {
		if (fab == "lattice") {
			fpga = new Lattice(jtag, args.bit_file, args.file_type,
				args.prg_type, args.flash_sector, args.verify, args.verbose, args.skip_load_bridge, args.skip_reset);
		} else {
			printError("Error: manufacturer " + fab + " not supported");
			delete(jtag);
			return EXIT_FAILURE;
		}
	} catch (std::exception &e) {
		printError("Error: Failed to claim FPGA device: " + string(e.what()));
		delete(jtag);
		return EXIT_FAILURE;
	}

	if ((!args.bit_file.empty() ||
		 !args.secondary_bit_file.empty() ||
		 !args.file_type.empty() || !args.mcufw.empty())
			&& args.prg_type != Device::RD_FLASH) {
		try {
			fpga->program(args.offset, args.unprotect_flash);
		} catch (std::exception &e) {
			printError("Error: Failed to program FPGA: " + string(e.what()));
			delete(fpga);
			delete(jtag);
			return EXIT_FAILURE;
		}
	}

	if (args.conmcu == true) {
		fpga->connectJtagToMCU();
	}

	/* read internal register */
	if (!args.read_register.empty()) {
		fpga->read_register(args.read_register);
	}

	/* unprotect SPI flash */
	if (args.unprotect_flash && args.bit_file.empty()) {
		fpga->unprotect_flash();
	}

	/* bulk erase SPI flash */
	if (args.bulk_erase_flash && args.bit_file.empty()) {
		fpga->bulk_erase_flash();
	}

	/* protect SPI flash */
	if (args.protect_flash != 0) {
		fpga->protect_flash(args.protect_flash);
	}

	if (args.prg_type == Device::RD_FLASH) {
		if (args.file_size == 0) {
			printError("Error: 0 size for dump");
		} else {
			fpga->dumpFlash(args.offset, args.file_size);
		}
	}

	if (args.reset)
		fpga->reset();

	delete(fpga);
	delete(jtag);
}

// parse double from string in engineering notation
// can deal with postfixes k and m, add more when required
static int parse_eng(string arg, double *dst) {
	try {
		size_t end;
		double base = stod(arg, &end);
		if (end == arg.size()) {
			*dst = base;
			return 0;
		} else if (end == (arg.size() - 1)) {
			switch (arg.back()) {
			case 'k': case 'K':
				*dst = (uint32_t)(1e3 * base);
				return 0;
			case 'm': case 'M':
				*dst = (uint32_t)(1e6 * base);
				return 0;
			default:
				return EINVAL;
			}
		} else {
			return EINVAL;
		}
	} catch (...) {
		cerr << "error : speed: invalid format" << endl;
		return EINVAL;
	}
}

/* arguments parser */
int parse_opt(int argc, char **argv, struct arguments *args,
	jtag_pins_conf_t *pins_config)
{
	string freqo, rd_reg;
	vector<string> pins, bus_dev_num;
	bool verbose, quiet;
	int8_t verbose_level = -2;
	try {
		cxxopts::Options options(argv[0], "openFPGALoader -- a program to flash FPGA",
			"<gwenhael.goavec-merou@trabucayre.com>");
		options
			.positional_help("BIT_FILE")
			.show_positional_help();

		options
			.add_options()
			("bitstream", "bitstream",
                               cxxopts::value<std::string>(args->bit_file))
			("c,cable", "jtag interface", cxxopts::value<string>(args->cable))
#if defined(USE_DEVICE_ARG)
			("d,device",  "device to use (/dev/ttyUSBx)",
				cxxopts::value<string>(args->device))
#endif
			("detect",      "detect FPGA",
				cxxopts::value<bool>(args->detect))
			("freq",        "jtag frequency (Hz)", cxxopts::value<string>(freqo))
			("f,write-flash",
				"write bitstream in flash (default: false)")
			("r,reset",   "reset FPGA after operations",
				cxxopts::value<bool>(args->reset))
			("unprotect-flash",   "Unprotect flash blocks",
				cxxopts::value<bool>(args->unprotect_flash))
			("v,verbose", "Produce verbose output", cxxopts::value<bool>(verbose))
			("h,help", "Give this help list")
			("V,Version", "Print program version");

		options.parse_positional({"bitstream"});
		auto result = options.parse(argc, argv);

		if (result.count("help")) {
			cout << options.help() << endl;
			return 1;
		}

		if (verbose && quiet) {
			printError("Error: can't select quiet and verbose mode in same time");
			throw std::exception();
		}
		if (verbose)
			args->verbose = 1;
		if (quiet)
			args->verbose = -1;
		if (verbose_level != -2) {
			if ((verbose && verbose_level != 1) ||
					(quiet && verbose_level != -1)) {
				printError("Error: mismatch quiet/verbose and verbose-level\n");
				throw std::exception();
			}

			args->verbose = verbose_level;
		}

		if (result.count("Version")) {
			cout << "openFPGALoader " << VERSION << endl;
			return 1;
		}

		else if (result.count("write-sram"))
			args->prg_type = Device::WR_SRAM;
		else if (result.count("external-flash"))
			args->prg_type = Device::WR_FLASH;

		if (result.count("freq")) {
			double freq;
			if (parse_eng(freqo, &freq)) {
				printError("Error: invalid format for --freq");
				throw std::exception();
			}
			if (freq < 1) {
				printError("Error: --freq must be positive");
				throw std::exception();
			}
			args->freq = static_cast<uint32_t>(freq);
		}

		if (result.count("pins")) {
			if (pins.size() != 4) {
				printError("Error: pin_config need 4 pins");
				throw std::exception();
			}

			static std::map <std::string, int> pins_list = {
				{"TXD", FT232RL_TXD},
				{"RXD", FT232RL_RXD},
				{"RTS", FT232RL_RTS},
				{"CTS", FT232RL_CTS},
				{"DTR", FT232RL_DTR},
				{"DSR", FT232RL_DSR},
				{"DCD", FT232RL_DCD},
				{"RI" , FT232RL_RI}};

			for (int i = 0; i < 4; i++) {
				int pin_num;
				try {
					pin_num = std::stoi(pins[i], nullptr, 0);
				} catch (std::exception &e) {
					if (pins_list.find(pins[i]) == pins_list.end()) {
						printError("Invalid pin name");
						throw std::exception();
					}
					pin_num = pins_list[pins[i]];
				}

				switch (i) {
					case 0:
						pins_config->tdi_pin = pin_num;
						break;
					case 1:
						pins_config->tdo_pin = pin_num;
						break;
					case 2:
						pins_config->tck_pin = pin_num;
						break;
					case 3:
						pins_config->tms_pin = pin_num;
						break;
				}
			}
			args->pin_config = true;
		}

		if (args->bit_file.empty() &&
			args->secondary_bit_file.empty() &&
			args->file_type.empty() &&
			args->mcufw.empty() &&
			!args->is_list_command &&
			!args->detect &&
			!args->protect_flash &&
			!args->unprotect_flash &&
			!args->bulk_erase_flash &&
			!args->xvc &&
			!args->reset &&
			!args->conmcu &&
			!args->read_dna &&
			!args->read_xadc &&
			args->read_register.empty()) {
			printError("Error: bitfile not specified");
			cout << options.help() << endl;
			throw std::exception();
		}
	} catch (const cxxopts::OptionException& e) {
		cerr << "Error parsing options: " << e.what() << endl;
		throw std::exception();
	}

	return 0;
}

