#include "fvc.h"
#include "bsp.h"
#include "fvc_protocol.h"
#include "fvc_eeprom.h"
#include "fvc_hash.h"
#include "fvc_backup_management.h"
#include "fvc_led.h"

#include "STM32_SPI_Bootloader/stm32_spi_bootloader.h"
#include "W25Q_Driver/Library/w25q_mem.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

// ------------------------------------------------
// macros

enum board_status
{
	STATUS_OK = 0,
	STATUS_BOOTLOADER_ERROR,
	STATUS_PROGRAM_INVALID,
	STATUS_EXECUTION_ERROR,
	STATUS_WAITING_FOR_NEW_PROGRAM,

	STATUS_TOP
};

#define ERASED_MEMORY_VALUE		0xFF

#define CLI_BUFFOR_LEN			256
#define DATA_OVERHEAD			7	// sfd, packet len, src_ID, dst_ID, packet type,, crc
#define MAX_PROGRAM_DATA_LEN	(2*1024) // data
//#define MAX_PROGRAM_DATA_LEN	256 // data

#define CFG_IGNORE_BACKUP		0
#define CFG_IGNORE_PROGRAM_HASH	0

#if !CFG_IGNORE_PROGRAM_HASH
static uint8_t hmac_sha256_key[] = {0x73, 0x65, 0x63, 0x72, 0x65, 0x74, 0x5f, 0x6b, 0x65, 0x79};
#endif

// ------------------------------------------------
// structures and unions

struct fvc_ctx
{
	// values read from EEPROM
	uint32_t config;
	uint8_t board_id;
	uint32_t firmware_version;

	// values change during program execution
	enum board_status status;
	bool interface_cli_data_present;
	uint8_t interface_cli_data[CLI_BUFFOR_LEN];
};

static struct fvc_ctx ctx = {
		.board_id = 0xff,
		.firmware_version = 0x00,
		.config = 0x00000000,
};

// ------------------------------------------------
// private functions

static void _interface_callback_handler();
static void _execute_frame_response(struct protocol_frame *frame);
static void _process_msg(void);
static void _get_board_info(void);
static bool _is_app_present_and_valid(void);
static bool _default_board_init(void);

// command handlers
static void _handle_update_program_request(struct protocol_frame *frame);


static void _interface_callback_handler(size_t len)
{
	static uint8_t data_buffor[CLI_BUFFOR_LEN];

	if ((len > 0) && !ctx.interface_cli_data_present) {
		memcpy(ctx.interface_cli_data, data_buffor, CLI_BUFFOR_LEN);
		ctx.interface_cli_data_present = true;
	} else if ((len > 0) && ctx.interface_cli_data_present)
	{
		send_response(false);
	}

	bsp_interface_receive_IT((uint8_t *)data_buffor, CLI_BUFFOR_LEN);
}

static void _execute_frame_response(struct protocol_frame *frame)
{
	switch (frame->data_type) {
		case TYPE_ID_REQ:
		case TYPE_ID_RESP:
		case TYPE_CLI_DATA:
			break;
		case TYPE_PROGRAM_UPDATE_REQUEST:
			_handle_update_program_request(frame);
			break;
		case TYPE_PROGRAM_DATA:
		case TYPE_EEPROM_DATA_READ:
		case TYPE_EEPROM_DATA_WRITE:
			break;
		default:
			break;
	}
}

static void _process_msg(void)
{
	uint8_t data[CLI_BUFFOR_LEN] = {0};
	struct protocol_frame frame;
	frame.payload_ptr = data;

	if (ctx.interface_cli_data_present) {
		if (frame_deserialize(&frame, ctx.interface_cli_data, CLI_BUFFOR_LEN)) {
			_execute_frame_response(&frame);
		} else {
			send_response(false);
		}

		memset(ctx.interface_cli_data, 0, CLI_BUFFOR_LEN);
		ctx.interface_cli_data_present = false;

		_interface_callback_handler(0);
	}
}

static void _get_board_info(void)
{
	uint32_t temp = 0;
	if (!fvc_eeprom_read(EEPROM_ID_ADDR, &temp)) {
		ctx.board_id = 0xFF;
	} else {
		ctx.board_id = (uint8_t) temp;
	}

	if (!fvc_eeprom_read(EEPROM_FIRMWARE_VERSION, &temp)) {
		ctx.firmware_version = 0;
	} else {
		ctx.firmware_version = temp;
	}

	if (!fvc_eeprom_read(EEPROM_CONFIG, &temp)) {
		ctx.config = 0x00000000;
	} else {
		ctx.config = temp;
	}

	fvc_protocol_init(ctx.board_id, ((uint8_t) ctx.config) & 0x03);
}

static bool _is_app_present_and_valid(void)
{
	uint32_t program_len, program_hash, calc_hash, current_addr;
	uint8_t prog_data[256];

	if (!fvc_eeprom_read(EEPROM_PROGRAM_LEN, &program_len) || !fvc_eeprom_read(EEPROM_PROGRAM_HASH, &program_hash)) {
		return false;
	}

	calc_hash = 0xFFFFFFFF;
	current_addr = APP_ADDR;

	while(current_addr < APP_ADDR + program_len)
	{
		size_t read_len;
		if ((current_addr + 256) >= (APP_ADDR + program_len)) {
			read_len = (APP_ADDR + program_len) - current_addr;
		}
		else
		{
			read_len = 256;
		}

		if (read_prog_memory(current_addr, prog_data, 256))
		{
			calc_hash = fvc_calc_crc(calc_hash, prog_data, read_len);
			current_addr += read_len;
		} else {
			jmp_to_bootloader();
		}
		HAL_Delay(5);
	}

	return calc_hash == program_hash;
}

static size_t _receive_and_deserialize_program_frame(uint8_t * data_out)
{
	uint8_t data[MAX_PROGRAM_DATA_LEN + DATA_OVERHEAD] = {0};

	struct protocol_frame packet;
	packet.payload_ptr = data_out;

	if (bsp_interface_receive(data, sizeof(data))) {
		if (frame_deserialize(&packet, data, sizeof(data))) {
			if ((packet.destination_id == ctx.board_id) && (packet.data_type == TYPE_PROGRAM_DATA)) {
				return packet.payload_len;
			}
		}
	}
	return 0;
}

static bool _compare_data(uint8_t *data1, uint8_t *data2, size_t data_len)
{
	size_t iterator =  0;
	while (iterator < data_len)
	{
		if (data1[iterator] != data2[iterator]) {
			return false;
		}
		iterator++;
	}
	return true;
}

static bool _handle_update_data(uint32_t packet_count)
{
	bool update_succesfull = false;





	return update_succesfull;
}

static void _handle_update_program_request(struct protocol_frame *frame)
{
	debug_transmit("Updating board\n\r");
	
	uint32_t memory_addr = APP_ADDR;
	uint8_t program_data[MAX_PROGRAM_DATA_LEN] = {0};
	uint8_t validation_data[256] = {0};
	size_t program_data_len = 0;

#if !CFG_IGNORE_PROGRAM_HASH
	uint8_t calc_program_hmac_sha256[32] = {0};
	fvc_calc_hmac_sha256_init(hmac_sha256_key, sizeof(hmac_sha256_key));
#endif

	uint8_t retry_counter = 0;

	uint32_t new_firmware_id = ((((uint32_t) frame->payload_ptr[0]) << 24)
			| (((uint32_t) frame->payload_ptr[1]) << 16)
			| (((uint32_t) frame->payload_ptr[2]) << 8)
			| ((uint32_t) frame->payload_ptr[3]));

	uint32_t packet_count = ((((uint32_t) frame->payload_ptr[4]) << 24)
			| (((uint32_t) frame->payload_ptr[5]) << 16)
			| (((uint32_t) frame->payload_ptr[6]) << 8)
			| ((uint32_t) frame->payload_ptr[7]));

	uint8_t program_hmac_sha256[32] = {0};
	memcpy(program_hmac_sha256, &frame->payload_ptr[8], 32);

	uint32_t prog_len = 0;
	uint32_t prog_hash = 0xFFFFFFFF;

	UNUSED(new_firmware_id);

	bsp_interface_abort_receive_IT();

	if (ctx.status == STATUS_OK) {
		if(!jmp_to_bootloader())
		{
			debug_transmit("Update aborted, bootloader faliure!\n\r");
			ctx.status = STATUS_BOOTLOADER_ERROR;
			send_response(false);
		}
	} else if (ctx.status == STATUS_BOOTLOADER_ERROR) {
		debug_transmit("Update aborted, bootloader faliure!\n\r");
		send_response(false);
		return;
	}

#if !CFG_IGNORE_BACKUP
	if (ctx.status == STATUS_OK) 
	{
		debug_transmit("Validating current backup\n\r");

		if (!validate_current_backup())
		{
			debug_transmit("Creating new backup\n\r");
			if (!create_firmware_backup())
			{
				debug_transmit("Failed to create program backup\n\r");
				send_response(false);
				return;
			} else {
				debug_transmit("Backup has been created\n\r");
			}
		} else {
			debug_transmit("Current backup is valid\n\r");
		}
	}
	else
	{
		debug_transmit("Current program invalid. Backup won't be created\n\r");
	}
#endif

	if (!erase_memory(0xFFFF, 0)) {
		debug_transmit("Update aborted, memory faliure!\n\r");
		ctx.status = STATUS_BOOTLOADER_ERROR;
		send_response(false);
		return;
	}

	debug_transmit("Erased memory\n\r");

	send_response(true);

	size_t counter = 0;
	while(counter < packet_count)
	{
		program_data_len = _receive_and_deserialize_program_frame(program_data);
		if (program_data_len)
		{
			debug_transmit("Received packet %d\n\r", counter);

			// TODO: sumarize program length anbd crc
			prog_len += program_data_len;
			prog_hash = fvc_calc_crc(prog_hash, program_data, program_data_len);

#if !CFG_IGNORE_PROGRAM_HASH
			fvc_calc_hmac_sha256_write_data(program_data, program_data_len);
#endif

			if ((program_data_len % 256) != 0)
			{
				memset(&program_data[program_data_len], 0xFF, MAX_PROGRAM_DATA_LEN - program_data_len);
			}

			size_t iterator = 0;
			while(iterator < MAX_PROGRAM_DATA_LEN) {

				if (write_memory(memory_addr, &program_data[iterator], 256)) {

					memset(validation_data, 0, 256);
					if(read_prog_memory(memory_addr, validation_data, 256))
					{
						if(_compare_data(&program_data[iterator], validation_data, 256))
						{
							memory_addr += 256;
							iterator += 256;
						}
					} else {
						jmp_to_bootloader();
					}
				} else {
					jmp_to_bootloader();
				}
			}
			counter++;
			send_response(true);
		}
		else
		{
			retry_counter++;
			debug_transmit("Failed to receive packet %d\n\r", counter);

			send_response(false);

			if(retry_counter > 3)
			{
				return;
			}
		}
	}

#if !CFG_IGNORE_PROGRAM_HASH
	fvc_calc_hmac_sha256_end_calc(calc_program_hmac_sha256);
	if (memcmp(calc_program_hmac_sha256, program_hmac_sha256, 32) == 0) 
	{
#endif

	jmp_to_app(APP_ADDR);
	ctx.status = STATUS_OK;

	fvc_eeprom_write(EEPROM_FIRMWARE_VERSION, new_firmware_id);
	fvc_eeprom_write(EEPROM_PROGRAM_LEN, prog_len);
	fvc_eeprom_write(EEPROM_PROGRAM_HASH, prog_hash);

	debug_transmit("Update finished\n\r");

#if !CFG_IGNORE_PROGRAM_HASH
	}
	else
	{
		ctx.status = STATUS_PROGRAM_INVALID;
		debug_transmit("Program hash is incorrect!\n\r");
	}
#endif

	return;
}

static void _print_program_statistics(void)
{
	uint32_t firmware_version, firmware_len, formware_hash;

	if (fvc_eeprom_read(EEPROM_FIRMWARE_VERSION, &firmware_version)
			&& fvc_eeprom_read(EEPROM_PROGRAM_LEN, &firmware_len)
			&& fvc_eeprom_read(EEPROM_PROGRAM_HASH, &formware_hash))
	{
		debug_transmit("Firmware statistics:\n\rFirmware version: %X\n\rFirmware length: %d\n\rFirmware hash: %X\n\r", firmware_version, firmware_len, formware_hash);
	}
}

static bool _default_board_init(void)
{
	debug_transmit("Connecting to bootlaoder...\n\r");
	if (!jmp_to_bootloader())
	{
		debug_transmit("ERROR: Bootloader connection failed\n\r");
		bsp_reset_gpio_controll(GPIO_RESET);
		ctx.status = STATUS_BOOTLOADER_ERROR;
		return false;
	} else {
		debug_transmit("Connected\n\r");
	}

	debug_transmit("Valdiating program\n\r");
	if (!_is_app_present_and_valid()) {
		debug_transmit("WARNING: Validation failed\n\r");
		ctx.status = STATUS_PROGRAM_INVALID;
		return false;
	} else {
		debug_transmit("Validation successful\n\r");
	}

	debug_transmit("Launching program\n\r");
	if (!jmp_to_app(APP_ADDR)) {
		debug_transmit("WARNING: Application execution failed\n\r");
		ctx.status = STATUS_EXECUTION_ERROR;
		return false;
	} else {
		ctx.status = STATUS_OK;
		debug_transmit("Application started\n\r");
	}

	_print_program_statistics();

	return true;
}

// ------------------------------------------------
// public functions

bool fvc_main(void)
{
	bsp_initi_gpio();
	fvc_led_program_init();

	bsp_interface_init(_interface_callback_handler);

	if (!fvc_eeprom_initialize())
	{
		// TODO: write eeprom failure handler
		return false;
	}

	fvc_eeprom_write(EEPROM_ID_ADDR, 1);
	fvc_eeprom_write(EEPROM_CONFIG, 3);

	_get_board_info();

	debug_transmit("FVC Init\n\r");

	if(W25Q_Init() != W25Q_OK)
	{
		return false;
	}

	// Enable QSPI for FLASH ext memory
	uint8_t buf[2] = {0};
	W25Q_ReadStatusReg(&buf[0], 1);
	W25Q_ReadStatusReg(&buf[1], 2);
	buf[1] |= (1<<1);
	W25Q_WriteStatusRegs(buf);

	if (!_default_board_init())
	{
		debug_transmit("WARNING: program could not be started\n\r");
	}

	debug_transmit("Started CLI\n\r");
	while(1)
	{
		_process_msg();
 
		fvc_led_cli_blink();
	}

	return true;
}
