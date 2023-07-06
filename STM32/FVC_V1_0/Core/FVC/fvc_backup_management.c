#include "fvc_backup_management.h"

#include "fvc.h"
#include "fvc_hash.h"
#include "fvc_eeprom.h"
#include "bsp.h"

#include "STM32_SPI_Bootloader/stm32_spi_bootloader.h"
#include "W25Q_Driver/Library/w25q_mem.h"

#include "quadspi.h"

bool create_firmware_backup(void)
{
	uint32_t prog_len, prog_hash, current_addr, ext_flash_addr;
	uint8_t prog_data[256];

	if (!fvc_eeprom_read(EEPROM_PROGRAM_LEN, &prog_len) || !fvc_eeprom_read(EEPROM_PROGRAM_HASH, &prog_hash))
	{
		return false;
	}

	if (!fvc_eeprom_write(EEPROM_BACKUP_PROGRAM_LEN, prog_len) || !fvc_eeprom_write(EEPROM_BACKUP_PROGRAM_HASH, prog_hash)) {
		return false;
	}

	current_addr = APP_ADDR;
	ext_flash_addr = 0;

	while(current_addr < APP_ADDR + prog_len)
	{
		size_t read_len;
		if ((current_addr + 256) >= (APP_ADDR + prog_len)) {
			read_len = (APP_ADDR + prog_len) - current_addr;
		}
		else
		{
			read_len = 256;
		}

		if (read_prog_memory(current_addr, prog_data, 256))
		{
			if (W25Q_ProgramRaw(prog_data, 256, ext_flash_addr) == W25Q_OK)
			{
				current_addr += read_len;
				ext_flash_addr += read_len;
			}

		} else {
			jmp_to_bootloader();
		}
//		HAL_Delay(5);
	}

	return true;
}

bool validate_current_backup(void)
{
	uint32_t prog_len, prog_hash, calc_hash, ext_flash_addr;
	uint8_t flash_data[256];

	if (!fvc_eeprom_read(EEPROM_BACKUP_PROGRAM_LEN, &prog_len) || !fvc_eeprom_read(EEPROM_BACKUP_PROGRAM_HASH, &prog_hash))
	{
		return false;
	}

	calc_hash = 0xFFFFFFFF;
	ext_flash_addr = 0;

	while(ext_flash_addr < prog_len)
	{
		size_t read_len;
		if ((ext_flash_addr + 256) >= (prog_len)) {
			read_len = prog_len - ext_flash_addr;
		}
		else
		{
			read_len = 256;
		}

		if (W25Q_ReadRaw(flash_data, 256, ext_flash_addr) == W25Q_OK)
		{
			calc_hash = fvc_calc_crc(calc_hash, flash_data, read_len);
			ext_flash_addr += read_len;
		}

		HAL_Delay(5);
	}

	return prog_hash == calc_hash;
}
