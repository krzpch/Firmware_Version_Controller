#include "fvc.h"
#include "bsp.h"
#include "fvc_protocol.h"
#include "fvc_eeprom.h"
#include "fvc_hash.h"
#include "fvc_backup_management.h"
#include "fvc_led.h"
#include "fvc_supervisor.h"

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

enum current_working_mode
{
	MODE_UPDATER = 0,
	MODE_SUPERVISOR = 1,

	MODE_TOP
};

#define ERASED_MEMORY_VALUE		0xFF

#define CLI_BUFFOR_LEN			256
#define DATA_OVERHEAD			7	// sfd, packet len, src_ID, dst_ID, packet type,, crc
#define MAX_PROGRAM_DATA_LEN	(2*1024) // data
//#define MAX_PROGRAM_DATA_LEN	256 // data

#if !CFG_IGNORE_PROGRAM_HASH
static uint8_t hmac_sha256_key[] = {0x73, 0x65, 0x63, 0x72, 0x65, 0x74, 0x5f, 0x6b, 0x65, 0x79};
#endif

// ------------------------------------------------
// structures and unions

struct fvc_ctx
{
	supervisor_t sup;
	enum current_working_mode curr_mode;

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

		.curr_mode = MODE_UPDATER,
};

// ------------------------------------------------
// private functions

static void _interface_callback_handler();
static void _execute_frame_response(struct protocol_frame *frame);
static void _process_msg(void);
static void _get_board_info(void);
static bool _is_app_present_and_valid(void);
static bool _default_board_init(void);
static void _reset_board(void);

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

static void _timer_elapsed_callback_handler()
{
	supervisor_timer_period_elapsed_callback(&ctx.sup);
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

#if CFG_CREATE_BACKUP_AT_START && !CFG_BUFFORING_MODE
	W25Q_EraseChip();
#endif

	calc_hash = 0xFFFFFFFF;
	current_addr = 0;

	while(current_addr < program_len)
	{
		size_t read_len;
		if ((current_addr + 256) >= program_len) {
			read_len = program_len - current_addr;
		}
		else
		{
			read_len = 256;
		}

		if (read_prog_memory(current_addr + APP_ADDR, prog_data, 256))
		{

#if CFG_CREATE_BACKUP_AT_START && !CFG_BUFFORING_MODE
			W25Q_ProgramRaw(prog_data, 256, current_addr);
#endif

			calc_hash = fvc_calc_crc(calc_hash, prog_data, read_len);
			current_addr += read_len;
		} else {
			jmp_to_bootloader();
		}
		HAL_Delay(5);
	}

#if CFG_CREATE_BACKUP_AT_START && !CFG_BUFFORING_MODE
	if (calc_hash == program_hash)
	{
		fvc_eeprom_write(EEPROM_BACKUP_PROGRAM_LEN, program_len);
		fvc_eeprom_write(EEPROM_BACKUP_PROGRAM_HASH, program_hash);
		return true;
	}
	return false;
#else
	return calc_hash == program_hash;
#endif
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

static bool _copy_program_from_flash_to_memory(void)
{
	uint8_t buff[256] = {0};
	uint32_t flash_prog_len, flash_prog_hash;
	uint32_t data_addr = 0;
	uint8_t retry_counter = 0;

	if(!fvc_eeprom_read(EEPROM_BACKUP_PROGRAM_LEN, &flash_prog_len) || !fvc_eeprom_read(EEPROM_BACKUP_PROGRAM_HASH, &flash_prog_hash))
	{
		ctx.status = STATUS_PROGRAM_INVALID;
		return false;
	}

	if(!jmp_to_bootloader())
	{
		ctx.status = STATUS_BOOTLOADER_ERROR;
		return false;
	}

	if (!erase_memory(0xFFFF, 0)) {
		ctx.status = STATUS_BOOTLOADER_ERROR;
		return false;
	}

	while (data_addr < flash_prog_len)
	{
		if(W25Q_ReadRaw(buff, 256, data_addr) == W25Q_OK)
		{
			if(write_memory(data_addr + APP_ADDR, buff, 256))
			{
				retry_counter = 0;
				data_addr += 256;
			}
			else
			{
				jmp_to_bootloader();
				retry_counter++;
				if (retry_counter > 3)
				{
					ctx.status = STATUS_PROGRAM_INVALID;
					return false;
				}
				
			}
		}
	}
	return true;
}

static void _decode_header_data(uint8_t *frame_payload ,uint32_t *new_firmware_id, uint32_t *packet_count, uint8_t *program_hmac_sha256)
{
	*new_firmware_id = ((((uint32_t) frame_payload[0]) << 24)
			| (((uint32_t) frame_payload[1]) << 16)
			| (((uint32_t) frame_payload[2]) << 8)
			| ((uint32_t) frame_payload[3]));

	*packet_count = ((((uint32_t) frame_payload[4]) << 24)
			| (((uint32_t) frame_payload[5]) << 16)
			| (((uint32_t) frame_payload[6]) << 8)
			| ((uint32_t) frame_payload[7]));

	memcpy(program_hmac_sha256, &frame_payload[8], 32);
}

#if CFG_BUFFORING_MODE
static void _handle_update_program_request(struct protocol_frame *frame)
{
	ctx.curr_mode = MODE_UPDATER;

	debug_transmit("Updating board\n\r");
	bool update_status = false;
	uint32_t memory_addr = 0;
	uint8_t program_data[MAX_PROGRAM_DATA_LEN] = {0};
	uint8_t validation_data[256] = {0};
	uint32_t new_firmware_id, packet_count;
	uint8_t program_hmac_sha256[32] = {0};
	uint32_t prog_len = 0;
	uint32_t prog_hash = 0xFFFFFFFF;

	size_t retry_counter = 0;
	size_t program_data_len = 0;

	bsp_interface_abort_receive_IT();

#if !CFG_IGNORE_PROGRAM_HASH
	uint8_t calc_program_hmac_sha256[32] = {0};
	fvc_calc_hmac_sha256_init(hmac_sha256_key, sizeof(hmac_sha256_key));
#endif

	_decode_header_data(frame->payload_ptr, &new_firmware_id ,&packet_count, program_hmac_sha256);

	W25Q_EraseChip();

	send_response(TYPE_ACK);

	size_t counter = 0;
	while(counter < packet_count)
	{
		program_data_len = _receive_and_deserialize_program_frame(program_data);
		if (program_data_len)
		{
			debug_transmit("Received packet %d\n\r", counter);

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

				if (W25Q_ProgramRaw(&program_data[iterator], 256, memory_addr) == W25Q_OK) {
					W25Q_ReadRaw(validation_data, 256, memory_addr);
					if (_compare_data(&program_data[iterator],validation_data,256))
					{
						memory_addr += 256;
						iterator += 256;
					}
					else
					{
						retry_counter++;
						if(retry_counter > 3)
						{
							send_response(TYPE_FATAL_ERROR);
							goto finish;
						}
					}
				}
			}
			counter++;
			send_response(TYPE_ACK);
		}
		else
		{
			retry_counter++;
			debug_transmit("Failed to receive packet %d\n\r", counter);

			if(retry_counter > 3)
			{
				send_response(TYPE_FATAL_ERROR);
				goto finish;
			}
			else
			{
				send_response(TYPE_NACK);
			}
		}
	}

#if !CFG_IGNORE_PROGRAM_HASH
	fvc_calc_hmac_sha256_end_calc(calc_program_hmac_sha256);
	if (memcmp(calc_program_hmac_sha256, program_hmac_sha256, 32) != 0) 
	{
		debug_transmit("Received program HMAC-SHA256 is incorrect!\n\r");
		send_response(TYPE_FATAL_ERROR);
		return;
	}
#endif

	update_status = true;
	update_status &= fvc_eeprom_write(EEPROM_BACKUP_PROGRAM_LEN, prog_len);
	update_status &= fvc_eeprom_write(EEPROM_BACKUP_PROGRAM_HASH, prog_hash);

finish:

	if (update_status)
	{
		ctx.curr_mode = MODE_UPDATER;
		bsp_updater_init();

		if (!_copy_program_from_flash_to_memory())
		{
			debug_transmit("Failed to save new program!\n\r");
			ctx.status = STATUS_PROGRAM_INVALID;

			return;
		}

		debug_transmit("Update finished. Executing app.\n\r");
		jmp_to_app(APP_ADDR);
		
		fvc_eeprom_write(EEPROM_FIRMWARE_VERSION, new_firmware_id);
		fvc_eeprom_write(EEPROM_PROGRAM_LEN, prog_len);
		fvc_eeprom_write(EEPROM_PROGRAM_HASH, prog_hash);
		ctx.status = STATUS_OK;

		send_response(TYPE_PROGRAM_UPDATE_FINISHED);

		ctx.curr_mode = MODE_SUPERVISOR;
		supervisor_init(&ctx.sup, &bsp_spi_transmit, &bsp_spi_receive, &bsp_timer_start_refresh, &_reset_board);
	}
	else
	{
		send_response(TYPE_FATAL_ERROR);
	}
}
#else
static void _handle_update_program_request(struct protocol_frame *frame)
{
	debug_transmit("Updating board\n\r");
	bool update_status = false;
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

	if(!jmp_to_bootloader())
	{
		debug_transmit("Update aborted, bootloader faliure!\n\r");
		ctx.status = STATUS_BOOTLOADER_ERROR;
		send_response(TYPE_FATAL_ERROR);
		return;
	}

#if !CFG_IGNORE_BACKUP
	if (ctx.status == STATUS_OK) 
	{
		debug_transmit("Validating current backup\n\r");

		if (!validate_current_backup(true))
		{
			debug_transmit("Creating new backup\n\r");
			if (!create_firmware_backup())
			{
				debug_transmit("Failed to create program backup\n\r");
				send_response(TYPE_FATAL_ERROR);
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
		send_response(TYPE_FATAL_ERROR);
		return;
	}

	debug_transmit("Erased memory\n\r");

	send_response(TYPE_ACK);

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
			send_response(TYPE_ACK);
		}
		else
		{
			retry_counter++;
			debug_transmit("Failed to receive packet %d\n\r", counter);

			if(retry_counter > 3)
			{
				send_response(TYPE_FATAL_ERROR);
				goto finish;
			}
			else
			{
				send_response(TYPE_NACK);
			}
		}
	}

#if !CFG_IGNORE_PROGRAM_HASH
	fvc_calc_hmac_sha256_end_calc(calc_program_hmac_sha256);
	if (memcmp(calc_program_hmac_sha256, program_hmac_sha256, 32) == 0) 
	{
#endif

	update_status = true;
	ctx.status = STATUS_OK;

	fvc_eeprom_write(EEPROM_FIRMWARE_VERSION, new_firmware_id);
	fvc_eeprom_write(EEPROM_PROGRAM_LEN, prog_len);
	fvc_eeprom_write(EEPROM_PROGRAM_HASH, prog_hash);

	debug_transmit("Update finished. Executiong app.\n\r");
	jmp_to_app(APP_ADDR);

#if !CFG_IGNORE_PROGRAM_HASH
	}
	else
	{
		ctx.status = STATUS_PROGRAM_INVALID;
		debug_transmit("Program hash is incorrect!\n\r");
	}
#endif

finish:

	if (!update_status)
	{
		debug_transmit("Update failed, returning to old program.\n\r");
		if (!_copy_program_from_flash_to_memory())
		{
			debug_transmit("Failed to return to old program!\n\r");
			ctx.status = STATUS_PROGRAM_INVALID;
		}
	}

	return;
}
#endif

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

	debug_transmit("Valdiating program...\n\r");
	if (!_is_app_present_and_valid()) {
		debug_transmit("WARNING: Validation failed\n\r");
		ctx.status = STATUS_PROGRAM_INVALID;
		return false;
	} else {
		debug_transmit("Validation successful\n\r");
	}

	debug_transmit("Launching program...\n\r");
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

static void _handle_invalid_program(void)
{
	if(ctx.status == STATUS_PROGRAM_INVALID && validate_current_backup(false))
	{
		ctx.curr_mode = MODE_UPDATER;
		bsp_timer_stop();
		bsp_updater_init();

		debug_transmit("Current firmware invalid. Restoring program from backup.\n\r");
		if (_copy_program_from_flash_to_memory())
		{
			jmp_to_app(APP_ADDR);
			ctx.status = STATUS_OK;
			debug_transmit("Firmware restored.\n\r");
		
			ctx.curr_mode = MODE_SUPERVISOR;
			supervisor_init(&ctx.sup, &bsp_spi_transmit, &bsp_spi_receive, &bsp_timer_start_refresh, &_reset_board);
		}
		else
		{
			debug_transmit("Failed to restore firmware.\n\r");
		}
	}
}

static void _reset_board(void)
{
    bsp_reset_gpio_controll(GPIO_RESET);
    HAL_Delay(10);
    bsp_reset_gpio_controll(GPIO_SET);
}

// ------------------------------------------------
// public functions

bool fvc_main(void)
{
	bsp_initi_gpio();
	fvc_led_program_init();

	bsp_interface_init(_interface_callback_handler);
	bsp_timer_init(_timer_elapsed_callback_handler);

	if (!fvc_eeprom_initialize())
	{
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
	else
	{
		ctx.curr_mode = MODE_SUPERVISOR;
		supervisor_init(&ctx.sup, &bsp_spi_transmit, &bsp_spi_receive, &bsp_timer_start_refresh, &_reset_board);
	}

	debug_transmit("Started CLI\n\r");
	while(1)
	{
		_handle_invalid_program();
		_process_msg();

		if (ctx.curr_mode == MODE_SUPERVISOR)
		{
			supervisor_loop(&ctx.sup);
		}

		//fvc_led_cli_blink();
	}

	return true;
}
