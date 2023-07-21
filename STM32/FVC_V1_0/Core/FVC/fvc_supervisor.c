/**
 * @file fvc_supervisor.c
 * @author Michał Tomacha
 * @brief 
 * @version 0.1
 * @date 2023-07-11
 * 
 * @copyright Copyright (c) 2023
 * 
 */

#include "fvc_supervisor.h"
#include "stdlib.h"

/*
    private helper functions
*/
static uint8_t receive_byte(supervisor_t* sup)
{
    uint8_t buf;
    
    if(sup->receive(&buf, 1, SPI_TIMEOUT_MS))
    {
        return buf;
    }
    
    return 0;
}

static supervision_command_t receive_command(supervisor_t* sup)
{
    uint8_t command;

    command = receive_byte(sup);

    if(command != 0)
    {
        return (supervision_command_t) command;
    }

    return supervision_command_top; 
}

static void send_response(supervisor_t* sup, bool ack)
{
    uint8_t resp = 1;

    if(!ack)
	{
    	resp = 0;
	}

    for(uint8_t i = 0; i < SPI_RETRY_CNT; i++)
    {
        if(sup->transmit(&resp, 1, SPI_TIMEOUT_MS))
        {
            return;
        }
    }

    return;
}

static int32_t receive_4_bytes(supervisor_t* sup)
{
    uint8_t buf[4] = {0};
    sup->receive(buf, 4, SPI_TIMEOUT_MS);
    return ((int32_t)buf[0]|(((int32_t)buf[1])<<8)|(((int32_t)buf[2])<<16)|(((int32_t)buf[3])<<24));
}

static bool receive_variable_range(supervisor_t* sup, uint8_t var_nb)
{
    if(var_nb >= sup->var_nb)
    {
        return false;
    }

    sup->supervision_variables[var_nb].min_val = (uint32_t)receive_4_bytes(sup);
    send_response(sup, true);
    sup->supervision_variables[var_nb].max_val = (uint32_t)receive_4_bytes(sup);
    send_response(sup, true);
    sup->supervision_variables[var_nb].checked = false;

    return true;
}

static bool receive_check_variable(supervisor_t* sup, uint8_t var_nb)
{
    if(var_nb >= sup->var_nb)
    {
        return false;
    }
    
    int32_t recv_var;
    recv_var = receive_4_bytes(sup);

    if((recv_var <= sup->supervision_variables[var_nb].max_val) && (recv_var >= sup->supervision_variables[var_nb].min_val))
    {
        sup->supervision_variables[var_nb].checked = true;
        return true;
    }

    return false;
}
/*
    private helper functions end
*/

void supervisor_init(supervisor_t* sup, transmit_t transmit, receive_t receive, timer_start_refresh_t timer_start_refresh, reset_t reset)
{   
    sup->state = supervisor_state_uninitialized;
    sup->transmit = transmit; 
    sup->receive = receive;
    sup->timer_start_refresh = timer_start_refresh;
    sup->reset = reset;

    sup->timer_start_refresh(INITIAL_RESET_TIMER_MS);
}

void supervisor_loop(supervisor_t* sup)
{
	static uint8_t configured_var_nb;
    supervision_command_t command;
    uint32_t period;

	switch(sup->state)
    {
    case supervisor_state_uninitialized:
        command = receive_command(sup);
        if(command == supervision_command_init) //wait for supervisee initial transmission
        {
            send_response(sup, true);
            sup->timer_start_refresh(INITIAL_RESET_TIMER_MS);
            sup->state = supervisor_state_awaiting_configuration;   
        }
        else if(command != supervision_command_top)
        {
            send_response(sup, false);
        }
    break;
    case supervisor_state_awaiting_configuration:
        command = receive_command(sup);
        if(command == supervision_command_set_variable_nb)
        {
            send_response(sup, true);
            sup->timer_start_refresh(INITIAL_RESET_TIMER_MS);
            if(sup->supervision_variables != NULL)
            {
                free(sup->supervision_variables);
            }
            sup->var_nb = 0;
            configured_var_nb = 0;
            sup->state = supervisor_state_setting_variables_nb;
        }
        else if(command == supervision_command_set_period)
        {
            send_response(sup, true);
            sup->timer_start_refresh(INITIAL_RESET_TIMER_MS);
            sup->state = supervisor_state_setting_period;
        }
        else if(command != supervision_command_top)
        {
            send_response(sup, false);
        }
    break;
    case supervisor_state_setting_variables_nb:
        sup->var_nb = receive_byte(sup);

        if(sup->var_nb != 0)
        {
			sup->supervision_variables = calloc(sup->var_nb, sizeof(supervision_variable_t));

			if(sup->supervision_variables == NULL)
			{
				send_response(sup, false);
				sup->state = supervisor_state_awaiting_configuration;
				break;
			}

			send_response(sup, true);
            sup->timer_start_refresh(INITIAL_RESET_TIMER_MS);
            sup->state = supervisor_state_setting_variables;
        }
        else
        {
        	send_response(sup, false);
        	sup->state = supervisor_state_awaiting_configuration;
        }
    break;
    case supervisor_state_setting_variables:
        if(receive_variable_range(sup, configured_var_nb++))
        {
            sup->timer_start_refresh(INITIAL_RESET_TIMER_MS);

            if(configured_var_nb >= sup->var_nb)
            {
                sup->state = supervisor_state_awaiting_configuration;
            }
        }
    break;
    case supervisor_state_setting_period:
    	period = (uint32_t)receive_4_bytes(sup);
    	if(period != 0)
    	{
        send_response(sup, true);
        sup->supervision_period = period;
        sup->timer_start_refresh(sup->supervision_period);
        sup->state = supervisor_state_awaiting_refresh;
    	}
    	else
    	{
    		send_response(sup, false);
    	}
    break;
    case supervisor_state_awaiting_refresh:
        command = receive_command(sup);
        if(command == supervision_command_refresh)
        {
            if(sup->var_nb != 0)
            {
                send_response(sup, true);
                sup->state = supervisor_state_check_variables;
                configured_var_nb = 0;
            }
            else
            {
            	send_response(sup, true);
            	sup->timer_start_refresh(sup->supervision_period);
            }   
        }
        else if(command == supervision_command_reconfigure)
        {
        	send_response(sup, true);
        	sup->timer_start_refresh(INITIAL_RESET_TIMER_MS);
            sup->state = supervisor_state_awaiting_configuration;
        }
        else if(command != supervision_command_top)
        {
            send_response(sup, false);
        }
    break;
    case supervisor_state_check_variables:
        if(receive_check_variable(sup, configured_var_nb++))
        {
            send_response(sup, true);
        }
        else
        {
            send_response(sup, false);
            sup->state = supervisor_state_awaiting_refresh;
        }

        bool all_checked = true;

        for(uint8_t i = 0; i < sup->var_nb; i++)
        {
            if(!sup->supervision_variables[i].checked)
            {
                all_checked = false;
            } 
        }

        if(all_checked)
        {
            sup->timer_start_refresh(sup->supervision_period); 
            for(uint8_t i = 0; i < sup->var_nb; i++)
            {
                sup->supervision_variables[i].checked = false;
            }
            sup->state = supervisor_state_awaiting_refresh;
        }

    break;
    case supervisor_state_resetting:
        sup->reset(); //reseting supervisee
        sup->state = supervisor_state_uninitialized;
        sup->timer_start_refresh(INITIAL_RESET_TIMER_MS);
    break;
    default:
        sup->state = supervisor_state_uninitialized;
    break;
    }
}

void supervisor_timer_period_elapsed_callback(supervisor_t* sup)
{  
    sup->state = supervisor_state_resetting;
}
