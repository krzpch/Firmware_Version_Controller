#include "fvc_led.h"
#include "bsp.h"

#define ARRAY_SIZE(_array) (sizeof(_array)/sizeof(_array[0]))

struct pattern_element 
{
    enum gpio_state state;
    size_t state_time_ms;
};

struct pattern_element pattern[] = 
    {
        {.state = GPIO_SET,     .state_time_ms = 100},
        {.state = GPIO_RESET,   .state_time_ms = 100},
        {.state = GPIO_SET,     .state_time_ms = 100},
        {.state = GPIO_RESET,   .state_time_ms = 700},
    };

static void _led_blink_callback_handler()
{
    static size_t curr_state = 0;

    bsp_led_gpio_controll(pattern[curr_state].state);
    bsp_led_timer_set_countdown(pattern[curr_state].state_time_ms);

    curr_state = (curr_state+1) % ARRAY_SIZE(pattern);
}

void fvc_led_init(void)
{
    bsp_led_timer_init(_led_blink_callback_handler);

    for (size_t i = 0; i < 2; i++)
    {
        bsp_led_gpio_controll(GPIO_SET);
        bsp_delay_ms(500);
        bsp_led_gpio_controll(GPIO_RESET);
        bsp_delay_ms(500);
    }  
}

void fvc_led_cli_blink(bool en)
{
    if (en)
    {
        bsp_led_timer_start();
    }
    else
    {
        bsp_led_timer_stop();
    }
}
