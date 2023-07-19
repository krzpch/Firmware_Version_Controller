#ifndef FVC_H
#define FVC_H

#include "stdbool.h"

// ------------------------------------
// Configuration parameters
#define APP_ADDR	0x08000000

#define CFG_BUFFORING_MODE          1

#define CFG_IGNORE_BACKUP		    0
#define CFG_CREATE_BACKUP_AT_START  1

#define CFG_IGNORE_PROGRAM_HASH	    0

bool fvc_main(void);

#endif
