#include "stm32_spi_bootloader.h"
#include "bsp.h"

#include <string.h>

// temp
#include "spi.h"

#define BOOTLOADER_SPI_ACK				0x79
#define BOOTLOADER_SPI_NACK				0x1F

#define BOOTLOADER_MAX_READ_WRITE		256

#define RESPONSE_TIMEOUT				1

#if RESPONSE_TIMEOUT
#define	MAX_RESPONSE_TIMEOUT			100	// timeout in ms
#define RESPONSE_RETRY_DELAY			1	// delay between retries in ms
#define MAX_RESPONSE_RETRY				(MAX_RESPONSE_TIMEOUT/RESPONSE_RETRY_DELAY)
#endif

#define SYNCRONIZATION_BYTE				0x5A

#define MAX_SUPPORTED_COMMANDS			32

#define GET_COMMAND 					0x00U
#define GET_VERSION_COMMAND 			0x01U
#define GET_ID_COMMAND 					0x02U
#define READ_MEMORY_COMMAND				0x11U
#define GO_COMMAND 						0x21U
#define WRITE_MEMORY_COMMAND			0x31U
#define ERASE_MEMORY_COMMAND			0x44U
#define SPECIAL_COMMAND					0x50U
#define EXTENDED_SPECIAL_COMMAND		0x51U
#define WRITE_PROTECT_COMMAND			0x63U
#define WRITE_UNPROTECT_COMMAND			0x73U
#define READ_PROTECT_COMMAND			0x82U
#define READ_UNPROTECT_COMMAND			0x92U
#define GET_CHECKSUM_COMMAND			0xA1U

#define SERIALIZE_ADDR(_addr)	{								\
									(uint8_t) (_addr >> (8*3)),	\
									(uint8_t) (_addr >> (8*2)),	\
									(uint8_t) (_addr >> (8*1)),	\
									(uint8_t) (_addr)			\
								}

enum bootloader_ret_val
{
	BOOTLOADER_NACK = 0,
	BOOTLOADER_ACK,
	BOOTLOADER_TIMEOUT,

	BOOTLOADER_TOP,
};

static uint8_t protocol_version = 0;
static uint8_t supported_commands_list[MAX_SUPPORTED_COMMANDS];

static uint8_t _calc_checksum(uint8_t *data, size_t data_len)
{
	uint8_t checksum = 0x00;
	for(size_t i = 0; i < data_len; i++)
	{
		checksum ^= data[i];
	}
	return checksum;
}

static uint8_t _calc_contnuus_checksum(uint8_t checksum_in, uint8_t *data, size_t data_len)
{
	uint8_t checksum = checksum_in;
	for(size_t i = 0; i < data_len; i++)
	{
		checksum ^= data[i];
	}
	return checksum;
}

static bool _is_command_supported(uint8_t command)
{
	for (int i = 0; i < MAX_SUPPORTED_COMMANDS; ++i) {
		if (supported_commands_list[i] == command) {
			return true;
		} else if (supported_commands_list[i] == 0xFF) {
			return false;
		}
	}

	return false;
}

static bool _send_command(uint8_t command)
{
	if ((command != GET_COMMAND) && !_is_command_supported(command)) {
		return false;
	}

	uint8_t data[3] = {SYNCRONIZATION_BYTE, command, ~command};

	return bsp_bootloader_transmit((uint8_t*)data, 3);
}

static enum bootloader_ret_val _get_reponse_procedure(void)
{
	enum bootloader_ret_val status = BOOTLOADER_TIMEOUT;

#if RESPONSE_TIMEOUT
	for (uint8_t var = 0; var < MAX_RESPONSE_RETRY; ++var) {
#else
	while(1) {
#endif
		uint8_t tx_data = 0;
		uint8_t rx_data = 0;

#if RESPONSE_TIMEOUT
		bsp_delay_ms(RESPONSE_RETRY_DELAY);
#endif
		bsp_bootloader_receive(&rx_data, 1);

		if (rx_data == BOOTLOADER_SPI_ACK) {
			tx_data = BOOTLOADER_SPI_ACK;
			bsp_bootloader_transmit(&tx_data, 1);
			status = BOOTLOADER_ACK;
			break;
		} else if (rx_data == BOOTLOADER_SPI_NACK) {
			tx_data = BOOTLOADER_SPI_NACK;
			bsp_bootloader_transmit(&tx_data, 1);
			status = BOOTLOADER_NACK;
			break;
		}
	}

	return status;
}

static bool _bootloader_syncronize(void)
{
	uint8_t tx_data = SYNCRONIZATION_BYTE;

	bsp_bootloader_transmit(&tx_data, 1);

	return _get_reponse_procedure() == BOOTLOADER_ACK;
}

static bool _bootloader_init_process(void)
{
	bool status = false;

	bsp_reset_gpio_controll(GPIO_RESET);
	bsp_boot0_gpio_controll(GPIO_SET);
	bsp_delay_ms(50);
	bsp_reset_gpio_controll(GPIO_SET);
	bsp_delay_ms(50);

	if (_bootloader_syncronize())
	{
		status = true;
	}

	bsp_delay_ms(25);
	bsp_boot0_gpio_controll(GPIO_RESET);

	return status;
}

static bool _receive_data(uint8_t * data, size_t data_len)
{
	uint8_t temp_rx_data = 0;

	bsp_bootloader_receive(&temp_rx_data, 1);
	if (temp_rx_data == 0xA5) {
		return bsp_bootloader_receive(data, data_len);
	}

	return false;
}

__attribute__((unused)) static bool _set_write_protect(void)
{
	// TODO: CREATE WRITE PROTECT
	return true;
}

__attribute__((unused)) static bool _set_write_unprotect(void)
{
	if (!_send_command(WRITE_UNPROTECT_COMMAND)) {
		return false;
	}

	if(_get_reponse_procedure() != BOOTLOADER_ACK)
	{
		return false;
	}

	if(_get_reponse_procedure() != BOOTLOADER_ACK)
	{
		return false;
	}

	return true;
}

__attribute__((unused)) static bool _set_read_protect(void)
{
	if (!_send_command(READ_UNPROTECT_COMMAND)) {
		return false;
	}

	if(_get_reponse_procedure() != BOOTLOADER_ACK)
	{
		return false;
	}

	if(_get_reponse_procedure() != BOOTLOADER_ACK)
	{
		return false;
	}

	return true;
}

__attribute__((unused)) static bool _set_read_unprotect(void)
{
	if (!_send_command(READ_PROTECT_COMMAND)) {
		return false;
	}

	if(_get_reponse_procedure() != BOOTLOADER_ACK)
	{
		return false;
	}

	if(_get_reponse_procedure() != BOOTLOADER_ACK)
	{
		return false;
	}

	return true;
}

__attribute__((unused)) static bool _get_version(uint8_t *version)
{
	if (!_send_command(GET_VERSION_COMMAND)) {
		return false;
	}

	if(_get_reponse_procedure() != BOOTLOADER_ACK)
	{
		return false;
	}

	bsp_bootloader_receive(&protocol_version, 1);

	if(_get_reponse_procedure() != BOOTLOADER_ACK)
	{
		return false;
	}

	return true;
}

static bool _get_version_and_supported_commands(void)
{
	uint8_t rx_data[MAX_SUPPORTED_COMMANDS + 1] = {0};
	uint8_t rx_len = 0;

	if (!_send_command(GET_COMMAND)) {
		return false;
	}

	if (_get_reponse_procedure() != BOOTLOADER_ACK) {
		return false;
	}

	for (int var = 0; var < 3; ++var) {
		bsp_bootloader_receive(&rx_len, 1);
		if (rx_len != 0xA5) {
			break;
		}
	}

	bsp_bootloader_receive((uint8_t *)rx_data, rx_len + 1);

	if (_get_reponse_procedure() != BOOTLOADER_ACK) {
		return false;
	}

	protocol_version = rx_data[0];

	memset(supported_commands_list, 0xFF, MAX_SUPPORTED_COMMANDS);
	for (uint8_t i = 0; i < rx_len; i++) {
		supported_commands_list[i] = rx_data[i + 1];
	}

	return true;
}

bool read_prog_memory(uint32_t addr, uint8_t *data, size_t data_len)
{
	if((data == NULL) || (data_len > BOOTLOADER_MAX_READ_WRITE))
	{
		return false;
	}

	uint8_t addr_data[4] = SERIALIZE_ADDR(addr);

	uint8_t checksum = _calc_checksum(addr_data, 4);
	uint8_t bytes_nb[2] = {(uint8_t) (data_len - 1), ~((uint8_t) (data_len - 1))};

	if (!_send_command(READ_MEMORY_COMMAND)) {
		return false;
	}

	if (_get_reponse_procedure() != BOOTLOADER_ACK) {
		return false;
	}

	bsp_bootloader_transmit((uint8_t*)addr_data, 4);
	bsp_bootloader_transmit(&checksum, 1);

	if (_get_reponse_procedure() != BOOTLOADER_ACK) {
		return false;
	}

	bsp_bootloader_transmit((uint8_t*)bytes_nb, 2);

	if (_get_reponse_procedure() != BOOTLOADER_ACK) {
		return false;
	}

	return _receive_data(data, data_len);
}

bool erase_memory(uint16_t sectors_count, uint16_t sectors_begin)
{
	if (sectors_count < 0xFFFD) {
		sectors_count -= 1;
	}

	uint8_t data[2] = {(uint8_t)(sectors_count >> 8), (uint8_t)(sectors_count)};
	uint8_t checksum = 0;

	if (!_send_command(ERASE_MEMORY_COMMAND)) {
		return false;
	}

	if (_get_reponse_procedure() != BOOTLOADER_ACK) {
		return false;
	}

	bsp_bootloader_transmit(data, 2);

	if (sectors_count >= 0xFFFD) {
		checksum = ~(uint8_t)(sectors_count);
		bsp_bootloader_transmit(&checksum, 1);
	} else {
		checksum = _calc_checksum(data, 2);
		bsp_bootloader_transmit(&checksum, 1);

		if (_get_reponse_procedure() != BOOTLOADER_ACK) {
			return false;
		}

		checksum = 0;
		for (uint16_t counter = sectors_begin; counter < sectors_begin + sectors_count + 1; counter++) {
			data[0] = (uint8_t)(counter >> 8);
			data[1] = (uint8_t)(counter);
			checksum = _calc_contnuus_checksum(checksum, data, 2);
			bsp_bootloader_transmit(data, 2);
		}
		bsp_bootloader_transmit(&checksum, 1);
	}

	if (_get_reponse_procedure() != BOOTLOADER_ACK) {
		return false;
	}

	return true;
}

bool write_memory(uint32_t addr, uint8_t *data, size_t data_len)
{
	if((data == NULL) || (data_len > BOOTLOADER_MAX_READ_WRITE))
	{
		return false;
	}

	uint8_t addr_data[4] = SERIALIZE_ADDR(addr);
	uint8_t checksum = _calc_checksum(addr_data, 4);
	uint8_t len_data = data_len - 1;

	if (!_send_command(WRITE_MEMORY_COMMAND)) {
		return false;
	}

	if (_get_reponse_procedure() != BOOTLOADER_ACK) {
		return false;
	}

	bsp_bootloader_transmit(addr_data, 4);
	bsp_bootloader_transmit(&checksum, 1);

	if (_get_reponse_procedure() != BOOTLOADER_ACK) {
		return false;
	}

	checksum = _calc_contnuus_checksum(len_data, data, data_len);
	bsp_bootloader_transmit(&len_data, 1);
	bsp_bootloader_transmit(data, data_len);
	bsp_bootloader_transmit(&checksum, 1);

	if (_get_reponse_procedure() != BOOTLOADER_ACK) {
		return false;
	}

	return true;
}

bool jmp_to_bootloader(void)
{
	if (!_bootloader_init_process())
	{
		return false;
	}

	if (!_get_version_and_supported_commands()) {
		return false;
	}

	return true;
}

bool jmp_to_app(uint32_t app_addr)
{
	uint8_t addr_data[4] = SERIALIZE_ADDR(app_addr);
	uint8_t checksum = _calc_checksum(addr_data, 4);

	if (!_send_command(GO_COMMAND)) {
		return false;
	}

	if (_get_reponse_procedure() != BOOTLOADER_ACK) {
		return false;
	}

	bsp_bootloader_transmit((uint8_t*)addr_data, 4);
	bsp_bootloader_transmit(&checksum, 1);

	if (_get_reponse_procedure() != BOOTLOADER_ACK) {
		return false;
	}

	return true;
}
