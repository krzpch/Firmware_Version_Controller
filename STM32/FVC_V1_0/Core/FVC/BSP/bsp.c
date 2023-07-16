#include "bsp.h"

#include "main.h"
#include "gpio.h"
#include "usart.h"
#include "spi.h"

// ---------------------------------------------------------------------------------
// comon support functions
void bsp_delay_ms(uint32_t time_ms)
{
	HAL_Delay(time_ms);
}

// ---------------------------------------------------------------------------------
// GPIO support functions

bool bsp_initi_gpio(void)
{
	HAL_GPIO_WritePin(PROC_BOOT0_GPIO_Port, PROC_BOOT0_Pin, GPIO_PIN_RESET);
	HAL_GPIO_WritePin(PROC_RESET_GPIO_Port, PROC_RESET_Pin, GPIO_PIN_RESET);

	return true;
}

bool bsp_boot0_gpio_controll(enum gpio_state state)
{

	HAL_GPIO_WritePin(PROC_BOOT0_GPIO_Port, PROC_BOOT0_Pin, (GPIO_PinState) state);
	return true;
}

bool bsp_reset_gpio_controll(enum gpio_state state)
{
	HAL_GPIO_WritePin(PROC_RESET_GPIO_Port, PROC_RESET_Pin, (GPIO_PinState) state);
	return true;
}

bool bsp_led_gpio_controll(enum gpio_state state)
{
	HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, (GPIO_PinState) state);
	return true;
}

// ---------------------------------------------------------------------------------
// bootloader support functions

#define BOOTLOADER_SPI_PTR 	&hspi2

bool bsp_bootloader_transmit(uint8_t * data, size_t data_len)
{
	bool status = false;
	status = HAL_SPI_Transmit(BOOTLOADER_SPI_PTR, data, data_len, 100) == HAL_OK;
	return status;
}

bool bsp_bootloader_receive(uint8_t * data, size_t max_data_len)
{
	bool status = false;
	status = HAL_SPI_Receive(BOOTLOADER_SPI_PTR, data, max_data_len, 500) == HAL_OK;
	return status;
}

// ----------------------------------------------------------------------------------
// CLI support functions

#define INTERFACE_UART_PTR &huart1

static void (*handler_ptr)(size_t) = NULL;

static void handler_func(struct __UART_HandleTypeDef * ptr, short unsigned int len)
{
	handler_ptr((size_t) len);
}

void bsp_interface_init(void (*handler)())
{
	handler_ptr = handler;
	HAL_StatusTypeDef status = HAL_UART_RegisterRxEventCallback(INTERFACE_UART_PTR, handler_func);
	if (status == HAL_OK)
	{
		handler(0);
	}
}

bool bsp_interface_transmit(uint8_t* data, size_t data_len)
{
	return HAL_UART_Transmit(INTERFACE_UART_PTR, (uint8_t*)data, data_len, 100) == HAL_OK;
}

bool bsp_interface_receive(uint8_t* data, size_t data_len)
{
	uint16_t temp;
	return HAL_UARTEx_ReceiveToIdle(INTERFACE_UART_PTR, (uint8_t*)data, data_len, &temp, 3000) == HAL_OK;
}

bool bsp_interface_receive_IT(uint8_t* data, size_t data_len)
{
	return HAL_UARTEx_ReceiveToIdle_IT(INTERFACE_UART_PTR, (uint8_t*)data, data_len) == HAL_OK;
}

bool bsp_interface_abort_receive_IT(void)
{
	return HAL_UART_AbortReceive(INTERFACE_UART_PTR) == HAL_OK;
}

// ----------------------------------------------------------------------------------
// DEBUG interface support functions

#define DEBUG_INTERFACE_UART_PTR &huart3

bool bsp_debug_interface_transmit(uint8_t* data, size_t data_len)
{
	return HAL_UART_Transmit(DEBUG_INTERFACE_UART_PTR, (uint8_t*)data, data_len, 100) == HAL_OK;
}
