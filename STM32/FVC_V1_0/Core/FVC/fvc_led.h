#ifndef FVC_LED_H
#define FVC_LED_H

#include "stdbool.h"

void fvc_led_init(void);
void fvc_led_cli_blink(bool en);

#endif