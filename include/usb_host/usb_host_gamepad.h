#ifndef __USB_HOST_GAMEPAD_H
#define __USB_HOST_GAMEPAD_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>

#include "gamepad_mapper.h"
#include "usb_host_config.h"

/* USB Gamepad Status */
#define GAMEPAD_DISCONNECT 0
#define GAMEPAD_CONNECTED 1
#define GAMEPAD_ENUMERATED 2

/* Gamepad Analog Thresholds & Default Values */
#define JOYSTICK_ANALOG_LOW_THRESH 0x40
#define JOYSTICK_ANALOG_HIGH_THRESH 0xC0
#define JOYSTICK_NEUTRAL_VAL_BYTE1 0x0F
#define JOYSTICK_NEUTRAL_VAL_BYTE2 0xFF

	/* 外部共有変数 */
	extern uint8_t Gamepad_Status;
	extern uint32_t Current_System_Time;
	extern volatile uint8_t Gamepad_Stable_Idx;
	extern uint8_t Gamepad_SPI_Data[2][3];
	extern uint8_t Gamepad_Raw_Report[2][64];
	extern uint8_t Gamepad_Raw_Report_Len[2];
	extern uint8_t Gamepad_Is_Switch_Clone;

	/* パブリック関数 */
	void USB_Host_Init_Sequence(void);
	void USBH_Process(void);
	void TIM3_Init(uint16_t arr, uint16_t psc);

#ifdef __cplusplus
}
#endif

#endif
