#include "fvc_led.h"
#include "bsp.h"

void fvc_led_program_init(void)
{
    for (size_t i = 0; i < 2; i++)
    {
        bsp_led_gpio_controll(GPIO_SET);
        bsp_delay_ms(500);
        bsp_led_gpio_controll(GPIO_RESET);
        bsp_delay_ms(500);
    }  
}

void fvc_led_cli_blink(void)
{
    for (size_t i = 0; i < 2; i++)
    {
        bsp_led_gpio_controll(GPIO_SET);
        bsp_delay_ms(200);
        bsp_led_gpio_controll(GPIO_RESET);
        bsp_delay_ms(200);
    }
    bsp_delay_ms(600);
}