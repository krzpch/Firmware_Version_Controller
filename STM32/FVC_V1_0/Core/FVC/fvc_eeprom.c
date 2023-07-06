#include "fvc_eeprom.h"

// ---------------------------------------------------------------------------------
// EEPROM support functions

#define EEPROM_ADDR 0xA0

bool fvc_eeprom_initialize(void)
{
	HAL_FLASH_Unlock();
	HAL_FLASH_OB_Unlock();
	EE_Status status = EE_Init(EE_FORCED_ERASE);
	return (EE_OK == status);
}

bool fvc_eeprom_read(enum eeprom_addr addr, uint32_t *data)
{
	EE_Status status = EE_ReadVariable32bits((uint16_t) addr, data);
	return status == EE_OK;
}

bool fvc_eeprom_write(enum eeprom_addr addr, uint32_t data)
{
	uint32_t current_data = 0;
	EE_Status status = EE_ReadVariable32bits((uint16_t) addr, &current_data);
	if (status != EE_OK && status != EE_NO_DATA) {
		return false;
	}
	if (data == current_data) {
		return true;
	}
	status = EE_WriteVariable32bits((uint16_t) addr, data);
	return status == EE_OK;
}
