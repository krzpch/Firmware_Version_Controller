#ifndef BSP_H
#define BSP_H

#include "stdbool.h"

#include "main.h"

enum gpio_state {
	GPIO_RESET = 0,
	GPIO_SET,

	GPIO_TOP
};

void bsp_delay_ms(uint32_t time_ms);

bool bsp_initi_gpio(void);
bool bsp_boot0_gpio_controll(enum gpio_state state);
bool bsp_reset_gpio_controll(enum gpio_state state);
bool bsp_led_gpio_controll(enum gpio_state state);

bool bsp_bootloader_transmit(uint8_t * data, size_t data_len);
bool bsp_bootloader_receive(uint8_t * data, size_t max_data_len);

void bsp_interface_init(void (*handler)());
bool bsp_interface_transmit(uint8_t* data, size_t data_len);
bool bsp_interface_receive(uint8_t* data, size_t data_len);
bool bsp_interface_receive_IT(uint8_t* data, size_t data_len);
bool bsp_interface_abort_receive_IT(void);

bool bsp_debug_interface_transmit(uint8_t* data, size_t data_len);

#endif
