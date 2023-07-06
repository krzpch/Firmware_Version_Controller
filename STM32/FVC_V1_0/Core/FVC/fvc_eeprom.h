#ifndef FVC_EEPROM_H
#define FVC_EEPROM_H

#include <stdbool.h>
#include "EEPROM_Emul/eeprom_emul.h"

enum eeprom_addr
{
	EEPROM_ID_ADDR = 1,
	EEPROM_FIRMWARE_VERSION,
	EEPROM_CONFIG,
	EEPROM_PROGRAM_LEN,
	EEPROM_PROGRAM_HASH,
	EEPROM_BACKUP_PROGRAM_LEN,
	EEPROM_BACKUP_PROGRAM_HASH,

	EEPROM_TOP
};

bool fvc_eeprom_initialize(void);
bool fvc_eeprom_read(enum eeprom_addr addr, uint32_t *data);
bool fvc_eeprom_write(enum eeprom_addr addr, uint32_t data);

#endif


