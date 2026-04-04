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

/* USB Gamepad Status */
#define GAMEPAD_DISCONNECT          0
#define GAMEPAD_CONNECTED           1
#define GAMEPAD_ENUMERATED          2

/* External Variables */
extern uint8_t Gamepad_Status;

/* Public Functions */
void USBH_MainInterrupt(void);
void USBH_Process(void);
void Gamepad_Timer_Init(uint16_t arr, uint16_t psc);

#ifdef __cplusplus
}
#endif

#endif
