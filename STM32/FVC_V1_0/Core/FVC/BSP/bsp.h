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

void bsp_led_timer_init(void (*handler)());
void bsp_led_timer_set_countdown(size_t value);
bool bsp_led_gpio_controll(enum gpio_state state);
void bsp_led_timer_start(void);
void bsp_led_timer_stop(void);

bool bsp_bootloader_transmit(uint8_t * data, size_t data_len);
bool bsp_bootloader_receive(uint8_t * data, size_t max_data_len);

void bsp_interface_init(void (*handler)());
bool bsp_interface_transmit(uint8_t* data, size_t data_len);
bool bsp_interface_receive(uint8_t* data, size_t data_len);
bool bsp_interface_receive_IT(uint8_t* data, size_t data_len);
bool bsp_interface_abort_receive_IT(void);

void bsp_timer_init(void (*handler)());

bool bsp_spi_transmit(uint8_t *data, uint16_t len, uint32_t timeout);
bool bsp_spi_receive(uint8_t *data, uint16_t len, uint32_t timeout);
void bsp_timer_start_refresh(uint32_t period);
bool bsp_timer_stop(void);

void bsp_updater_init(void);
void bsp_supervisor_init(void);

bool bsp_debug_interface_transmit(uint8_t* data, size_t data_len);

#endif
