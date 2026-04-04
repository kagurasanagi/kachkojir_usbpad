/********************************** (C) COPYRIGHT *******************************
 * File Name          : usb_host_gamepad.h
 * Author             : Antigravity
 * Version            : V1.0.0
 * Date               : 2026/04/04
 * Description        : USB Host Gamepad handling header.
 *******************************************************************************/

#ifndef __USB_HOST_GAMEPAD_H
#define __USB_HOST_GAMEPAD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "usb_host_config.h"
#include <stdint.h>

/* USB Gamepad Status */
#define GAMEPAD_DISCONNECT          0
#define GAMEPAD_CONNECTED           1
#define GAMEPAD_ENUMERATED          2

/* External Variables */
extern uint8_t Gamepad_Status;
extern uint32_t Current_System_Time;

/* Public Functions */
void USB_Host_Init_Sequence(void);
void USBH_MainInterrupt(void);
void USBH_Process(void);
void TIM3_Init(uint16_t arr, uint16_t psc);

#ifdef __cplusplus
}
#endif

#endif
