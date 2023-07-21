
#ifndef FVC_BACKUP_MANAGEMENT_H
#define BACKUP_MANAGEMENT_H

#include <stdbool.h>

bool create_firmware_backup(void);
bool validate_current_backup(bool compare_with_current_program);

#endif
