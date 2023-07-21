/**
 * @file fvc_supervisor.h
 * @author Michał Tomacha
 * @brief 
 * @version 0.1
 * @date 2023-07-11
 * 
 * @copyright Copyright (c) 2023
 * 
 */

#ifndef SUPERVISOR_H
#define SUPERVISOR_H

#include "stdint.h"
#include "stdbool.h"

#define SPI_TIMEOUT_MS 5000
#define SPI_RETRY_CNT 5
#define INITIAL_RESET_TIMER_MS 10000

typedef bool (*transmit_t)(uint8_t *data, uint16_t len, uint32_t timeout);
typedef bool (*receive_t)(uint8_t *data, uint16_t len, uint32_t timeout);
typedef void (*timer_start_refresh_t)(uint32_t period);
typedef void (*reset_t)(void);

typedef enum {
    supervisor_state_uninitialized,
    supervisor_state_awaiting_configuration,
    supervisor_state_setting_variables_nb,
    supervisor_state_setting_variables,
    supervisor_state_setting_period,
    supervisor_state_awaiting_refresh,
    supervisor_state_check_variables,
    supervisor_state_resetting
} supervisor_state_t;

typedef enum {
    supervision_command_init = 1,
    supervision_command_set_variable_nb,
    supervision_command_set_variable,
    supervision_command_set_period,
    supervision_command_refresh,
    supervision_command_reconfigure,
    supervision_command_top
} supervision_command_t;

typedef struct {
    bool checked;
    int32_t min_val;
    int32_t max_val;
} supervision_variable_t;

typedef struct {
    transmit_t transmit;
    receive_t receive;
    timer_start_refresh_t timer_start_refresh;
    reset_t reset;
    supervisor_state_t state;
    uint32_t supervision_period;
    uint8_t var_nb;
    supervision_variable_t* supervision_variables;
} supervisor_t;

void supervisor_init(supervisor_t* sup, transmit_t transmit, receive_t receive, timer_start_refresh_t timer_start_refresh, reset_t reset);
void supervisor_loop(supervisor_t* sup);
void supervisor_timer_period_elapsed_callback(supervisor_t* sup);

#endif
