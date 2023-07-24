#include "bsp.h"

#include "main.h"
#include "gpio.h"
#include "usart.h"
#include "spi.h"
#include "tim.h"

#include "stm32g491xx.h"
#include "stm32g4xx_hal_spi.h"
#include "stm32g4xx_hal_tim.h"

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

// ---------------------------------------------------------------------------------
// LED support functions

#define BOOTLOADER_LED_PTR 	&htim2

bool bsp_led_gpio_controll(enum gpio_state state)
{
	HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, (GPIO_PinState) state);
	return true;
}

static void (*led_handler_ptr)(void) = NULL;

static void led_handler_func(TIM_HandleTypeDef *htim)
{
	if (htim == BOOTLOADER_LED_PTR)
	{
		led_handler_ptr();
	}
}

void bsp_led_timer_init(void (*handler)())
{
	led_handler_ptr = handler;
	HAL_TIM_RegisterCallback(BOOTLOADER_LED_PTR, HAL_TIM_PERIOD_ELAPSED_CB_ID, led_handler_func);
}

void bsp_led_timer_set_countdown(size_t value)
{
	HAL_TIM_Base_Stop(BOOTLOADER_LED_PTR);
	__HAL_TIM_SET_COUNTER(BOOTLOADER_LED_PTR, value);
	HAL_TIM_Base_Start(BOOTLOADER_LED_PTR);
}

void bsp_led_timer_start(void)
{
	HAL_TIM_Base_Start_IT(BOOTLOADER_LED_PTR);
}

void bsp_led_timer_stop(void)
{
	HAL_TIM_Base_Stop_IT(BOOTLOADER_LED_PTR);
}

// ---------------------------------------------------------------------------------
// bootloader support functions

#define BOOTLOADER_SUPERVISOR_SPI_PTR 	&hspi2

bool bsp_bootloader_transmit(uint8_t * data, size_t data_len)
{
	bool status = false;
	status = HAL_SPI_Transmit(BOOTLOADER_SUPERVISOR_SPI_PTR, data, data_len, 100) == HAL_OK;
	return status;
}

bool bsp_bootloader_receive(uint8_t * data, size_t max_data_len)
{
	bool status = false;
	status = HAL_SPI_Receive(BOOTLOADER_SUPERVISOR_SPI_PTR, data, max_data_len, 500) == HAL_OK;
	return status;
}

// ----------------------------------------------------------------------------------
// Supervisor interface

#define BOOTLOADER_SUPERVISOR_TIMER_PTR 	&htim1

static void (*timer_handler_ptr)(void) = NULL;

static void timer_handler_func(TIM_HandleTypeDef *htim)
{
	if (htim == BOOTLOADER_SUPERVISOR_TIMER_PTR)
	{
		timer_handler_ptr();
	}
}

void bsp_timer_init(void (*handler)())
{
	timer_handler_ptr = handler;
	HAL_TIM_RegisterCallback(BOOTLOADER_SUPERVISOR_TIMER_PTR, HAL_TIM_PERIOD_ELAPSED_CB_ID, timer_handler_func);
}

bool bsp_spi_transmit(uint8_t *data, uint16_t len, uint32_t timeout)
{
    if(HAL_SPI_Transmit(BOOTLOADER_SUPERVISOR_SPI_PTR, data, len, timeout) != HAL_OK)
    {
        return false;
    }
    else
    {
        return true;
    }
}

bool bsp_spi_receive(uint8_t *data, uint16_t len, uint32_t timeout)
{
    if(HAL_SPI_Receive(BOOTLOADER_SUPERVISOR_SPI_PTR, data, len, timeout) != HAL_OK)
    {
        return false;
    }
    else
    {
        return true;
    }
}

void bsp_timer_start_refresh(uint32_t period)
{
    HAL_TIM_Base_Start_IT(BOOTLOADER_SUPERVISOR_TIMER_PTR);
    if(period > 65535)
    {
        __HAL_TIM_SET_COUNTER(BOOTLOADER_SUPERVISOR_TIMER_PTR, 65535);
    }
    else
    {
        __HAL_TIM_SET_COUNTER(BOOTLOADER_SUPERVISOR_TIMER_PTR, period);
    }
}

bool bsp_timer_stop(void)
{
	return HAL_TIM_Base_Stop_IT(BOOTLOADER_SUPERVISOR_TIMER_PTR) == HAL_OK;
}

void bsp_updater_init(void)
{
	HAL_SPI_DeInit(&hspi2);

	hspi2.Instance = SPI2;
	hspi2.Init.Mode = SPI_MODE_MASTER;
	hspi2.Init.Direction = SPI_DIRECTION_2LINES;
	hspi2.Init.DataSize = SPI_DATASIZE_8BIT;
	hspi2.Init.CLKPolarity = SPI_POLARITY_LOW;
	hspi2.Init.CLKPhase = SPI_PHASE_1EDGE;
	hspi2.Init.NSS = SPI_NSS_HARD_OUTPUT;
	hspi2.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_64;
	hspi2.Init.FirstBit = SPI_FIRSTBIT_MSB;
	hspi2.Init.TIMode = SPI_TIMODE_DISABLE;
	hspi2.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
	hspi2.Init.CRCPolynomial = 7;
	hspi2.Init.CRCLength = SPI_CRC_LENGTH_DATASIZE;
	hspi2.Init.NSSPMode = SPI_NSS_PULSE_ENABLE;
	if (HAL_SPI_Init(&hspi2) != HAL_OK)
	{
		Error_Handler();
	}
}

void bsp_supervisor_init(void)
{
	HAL_SPI_DeInit(&hspi2);

	hspi2.Instance = SPI2;
	hspi2.Init.Mode = SPI_MODE_SLAVE;
	hspi2.Init.Direction = SPI_DIRECTION_2LINES;
	hspi2.Init.DataSize = SPI_DATASIZE_8BIT;
	hspi2.Init.CLKPolarity = SPI_POLARITY_LOW;
	hspi2.Init.CLKPhase = SPI_PHASE_1EDGE;
	hspi2.Init.NSS = SPI_NSS_HARD_INPUT;
	hspi2.Init.FirstBit = SPI_FIRSTBIT_MSB;
	hspi2.Init.TIMode = SPI_TIMODE_DISABLE;
	hspi2.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
	hspi2.Init.CRCPolynomial = 7;
	hspi2.Init.CRCLength = SPI_CRC_LENGTH_DATASIZE;
	hspi2.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
	if (HAL_SPI_Init(&hspi2) != HAL_OK)
	{
		Error_Handler();
	}
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
