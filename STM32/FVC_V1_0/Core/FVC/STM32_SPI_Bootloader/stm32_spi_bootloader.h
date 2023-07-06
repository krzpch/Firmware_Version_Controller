#ifndef STM32_SPI_BOOTLOADER_H
#define STM32_SPI_BOOTLOADER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define	STM32_FLASH_START_ADDR	0x08000000

// @note user can only read up to 256 bytes in one readout
bool read_prog_memory(uint32_t addr, uint8_t *data, size_t data_len);

// @note if sectors_count equals to 0xFFFF global mass erase will be performed
bool erase_memory(uint16_t sectors_count, uint16_t sectors_begin);

bool write_memory(uint32_t addr, uint8_t *data, size_t data_len);

bool jmp_to_bootloader(void);
bool jmp_to_app(uint32_t app_addr);

#endif
