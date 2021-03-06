/* Copyright (c) 2014, Nordic Semiconductor ASA
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/** @file
  @brief Implementation of the ACI library.
 */

#include <stdlib.h>
#include <string.h>
#include <avr/eeprom.h>
#include <util/delay.h>

#include "hal_platform.h"
#include "aci.h"
#include "aci_cmds.h"
#include "aci_evts.h"
#include "aci_protocol_defines.h"
#include "acilib.h"
#include "acilib_defs.h"
#include "acilib_if.h"
#include "hal_aci_tl.h"
#include "lib_aci.h"

#define LIB_ACI_DEFAULT_CREDIT_NUMBER   1

extern uint8_t need_tx;
extern uint8_t rx_ready;

extern hal_aci_data_t received_data;

/*
Global additionally used used in aci_setup
*/

//bool lib_aci_is_pipe_available(aci_state_t *aci_stat, uint8_t pipe)
//{
//  uint8_t byte_idx;
//
//  byte_idx = pipe / 8;
//  if (aci_stat->pipes_open_bitmap[byte_idx] & (0x01 << (pipe % 8)))
//  {
//    return(true);
//  }
//  return(false);
//}

bool lib_aci_radio_reset(aci_state_t *aci_stat)
{
  hal_aci_data_t  msg_to_send;
  uint8_t *buffer = &(msg_to_send.buffer[0]);

  *(buffer + OFFSET_ACI_CMD_T_LEN) = MSG_BASEBAND_RESET_LEN;
  *(buffer + OFFSET_ACI_CMD_T_CMD_OPCODE) = ACI_CMD_RADIO_RESET;

  return hal_aci_tl_send(&msg_to_send);
}

void lib_aci_init(aci_state_t *aci_stat)
{
  uint8_t i;

  for (i = 0; i < PIPES_ARRAY_SIZE; i++)
  {
    aci_stat->pipes_open_bitmap[i]          = 0;
    aci_stat->pipes_closed_bitmap[i]        = 0;
  }

  hal_aci_tl_init();
  
  /* If RDYN is not low, there is no message pending on the nrF8001, at which
   * point we should performa a radio reset to get in a known state.
   */
  //if (!hal_aci_tl_rdyn())
  //{
      hal_aci_evt_t aci_data;
      
      lib_aci_radio_reset(aci_stat);
      
      while (1)
      {
          //Wait for the command response of the radio reset command.
          //as the nRF8001 will be in either SETUP or STANDBY after the ACI Reset Radio is processe
          if (true == lib_aci_event_get(aci_stat, &aci_data))
          {
              aci_evt_t * aci_evt;
              aci_evt = &(aci_data.evt);
              
              if (ACI_EVT_CMD_RSP == aci_evt->evt_opcode)
              {
                  hal_aci_data_t  received_msg;
                  
                  //Inject a Device Started Event Setup to the ACI Event Queue
                  received_msg.status_byte = 0;
                  received_msg.buffer[0] = 4;    //Length
                  received_msg.buffer[1] = 0x81; //Device Started Event
                  //if (ACI_STATUS_ERROR_DEVICE_STATE_INVALID == aci_evt->params.cmd_rsp.cmd_status || ACI_STATUS_SUCCESS == aci_evt->params.cmd_rsp.cmd_status) //in SETUP
                  //{
                      received_msg.buffer[2] = 0x02; //Setup
                  //}
                  //else if () //We are now in STANDBY
                  //{
                  //    received_msg.buffer[2] = 0x03; //Standby
                  //}
                  received_msg.buffer[3] = 0;    //Hardware Error -> None
                  received_msg.buffer[4] = 2;    //Data Credit Available
                  
                  memcpy((uint8_t *)&(received_data), (uint8_t *)&(received_msg), sizeof(hal_aci_data_t));
                  rx_ready = 1;
                  //Break out of the while loop
                  break;
              } 
          }
      }
  //}
}

bool lib_aci_connect(uint16_t run_timeout, uint16_t adv_interval)
{
  hal_aci_data_t  msg_to_send;
  aci_cmd_params_connect_t aci_cmd_params_connect;

  uint8_t *buffer = &(msg_to_send.buffer[0]);

  aci_cmd_params_connect.timeout      = run_timeout;
  aci_cmd_params_connect.adv_interval = adv_interval;

  *(buffer + OFFSET_ACI_CMD_T_LEN) = MSG_CONNECT_LEN;
  *(buffer + OFFSET_ACI_CMD_T_CMD_OPCODE) = ACI_CMD_CONNECT;

  *(buffer + OFFSET_ACI_CMD_T_CONNECT +
      OFFSET_ACI_CMD_PARAMS_CONNECT_T_TIMEOUT_MSB) =
    (uint8_t)(aci_cmd_params_connect.timeout >> 8);

  *(buffer + OFFSET_ACI_CMD_T_CONNECT +
      OFFSET_ACI_CMD_PARAMS_CONNECT_T_TIMEOUT_LSB) =
    (uint8_t)(aci_cmd_params_connect.timeout);

  *(buffer + OFFSET_ACI_CMD_T_CONNECT +
      OFFSET_ACI_CMD_PARAMS_CONNECT_T_ADV_INTERVAL_MSB) =
    (uint8_t)(aci_cmd_params_connect.adv_interval >> 8);

  *(buffer + OFFSET_ACI_CMD_T_CONNECT +
      OFFSET_ACI_CMD_PARAMS_CONNECT_T_ADV_INTERVAL_LSB) =
    (uint8_t)(aci_cmd_params_connect.adv_interval);

  return hal_aci_tl_send(&msg_to_send);
}

bool lib_aci_disconnect(aci_state_t *aci_stat, aci_disconnect_reason_t reason)
{
  bool ret_val;
  uint8_t i;
  hal_aci_data_t  msg_to_send;
  uint8_t *buffer = &(msg_to_send.buffer[0]);

  aci_cmd_params_disconnect_t aci_cmd_params_disconnect;
  aci_cmd_params_disconnect.reason = reason;

  *(buffer + OFFSET_ACI_CMD_T_LEN) = MSG_DISCONNECT_LEN;
  *(buffer + OFFSET_ACI_CMD_T_CMD_OPCODE) = ACI_CMD_DISCONNECT;

  *(buffer + OFFSET_ACI_CMD_T_DISCONNECT +
      OFFSET_ACI_CMD_PARAMS_DISCONNECT_T_REASON) =
    (uint8_t)(aci_cmd_params_disconnect.reason);

  ret_val = hal_aci_tl_send(&msg_to_send);
  /* If we have actually sent the disconnect */
  if (ret_val)
  {
    /* Update pipes immediately so that while the disconnect is happening,
     * the application can't attempt sending another message
     * If the application sends another message before we updated this
     * a ACI Pipe Error Event will be received from nRF8001
     */
    for (i=0; i < PIPES_ARRAY_SIZE; i++)
    {
      aci_stat->pipes_open_bitmap[i] = 0;
      aci_stat->pipes_closed_bitmap[i] = 0;
    }
  }
  return ret_val;
}

bool lib_aci_send_data(uint8_t pipe, uint8_t *p_value, uint8_t size)
{
  aci_cmd_params_send_data_t aci_cmd_params_send_data;
  hal_aci_data_t  msg_to_send;
  uint8_t *buffer = &(msg_to_send.buffer[0]);

  aci_cmd_params_send_data.tx_data.pipe_number = pipe;
  memcpy(&(aci_cmd_params_send_data.tx_data.aci_data[0]), p_value, size);

  *(buffer + OFFSET_ACI_CMD_T_LEN) = MSG_SEND_DATA_BASE_LEN + size;
  *(buffer + OFFSET_ACI_CMD_T_CMD_OPCODE) = ACI_CMD_SEND_DATA;

  *(buffer + OFFSET_ACI_CMD_T_SEND_DATA +
      OFFSET_ACI_CMD_PARAMS_SEND_DATA_T_TX_DATA +
      OFFSET_ACI_TX_DATA_T_PIPE_NUMBER) =
    aci_cmd_params_send_data.tx_data.pipe_number;

  memcpy((buffer + OFFSET_ACI_CMD_T_SEND_DATA +
        OFFSET_ACI_CMD_PARAMS_SEND_DATA_T_TX_DATA +
        OFFSET_ACI_TX_DATA_T_ACI_DATA),
      &(aci_cmd_params_send_data.tx_data.aci_data[0]), size);

  return hal_aci_tl_send(&msg_to_send);
}

bool lib_aci_event_get(aci_state_t *aci_stat, hal_aci_evt_t *p_aci_evt_data)
{
  bool status = false;

  status = hal_aci_tl_event_get((hal_aci_data_t *)p_aci_evt_data);

    /**
     Update the state of the ACI with the
     ACI Events -> Pipe Status, Disconnected, Connected, Bond Status, Pipe Error
     */
    if (true == status)
    {
        aci_evt_t * aci_evt;
        
        aci_evt = &p_aci_evt_data->evt;
        
        switch(aci_evt->evt_opcode)
        {
            case ACI_EVT_PIPE_STATUS:
            {
                uint8_t i=0;
                
                for (i=0; i < PIPES_ARRAY_SIZE; i++)
                {
                    aci_stat->pipes_open_bitmap[i]   = aci_evt->params.pipe_status.pipes_open_bitmap[i];
                    aci_stat->pipes_closed_bitmap[i] = aci_evt->params.pipe_status.pipes_closed_bitmap[i];
                }
            }
                break;
                
            case ACI_EVT_DISCONNECTED:
            {
                uint8_t i=0;
                
                for (i=0; i < PIPES_ARRAY_SIZE; i++)
                {
                    aci_stat->pipes_open_bitmap[i] = 0;
                    aci_stat->pipes_closed_bitmap[i] = 0;
                }
                aci_stat->confirmation_pending = false;
                aci_stat->data_credit_available = aci_stat->data_credit_total;
                
            }
                break;
                
            case ACI_EVT_TIMING:
                aci_stat->connection_interval = aci_evt->params.timing.conn_rf_interval;
                aci_stat->slave_latency       = aci_evt->params.timing.conn_slave_rf_latency;
                aci_stat->supervision_timeout = aci_evt->params.timing.conn_rf_timeout;
                break;
                
            default:
                /* Need default case to avoid compiler warnings about missing enum
                 * values on some platforms.
                 */
                break;
        }
    }
    
  return status;
}

bool lib_aci_event_peek(hal_aci_evt_t *p_aci_evt_data)
{
    return hal_aci_tl_event_peek((hal_aci_data_t *)p_aci_evt_data);
}

