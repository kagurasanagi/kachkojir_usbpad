/********************************** (C) COPYRIGHT *******************************
 * ファイル名          : usb_host_gamepad.h
 * 著者                : Antigravity
 * バージョン          : V1.0.0
 * 日付                : 2026/04/04
 * 説明                : USB ホスト ゲームパッド 処理 ヘッダー。
 *******************************************************************************/

#ifndef __USB_HOST_GAMEPAD_H
#define __USB_HOST_GAMEPAD_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>

#include "usb_host_config.h"

/* USB ゲームパッド ステータス */
#define GAMEPAD_DISCONNECT 0
#define GAMEPAD_CONNECTED 1
#define GAMEPAD_ENUMERATED 2

	/* 外部共有変数 */
	extern uint8_t Gamepad_Status;
	extern uint32_t Current_System_Time;
	extern volatile uint8_t Gamepad_Stable_Idx;
	extern uint8_t Gamepad_SPI_Data[2][3];
	extern uint8_t Gamepad_Raw_Report[2][64];
	extern uint8_t Gamepad_Raw_Report_Len[2];

	/* パブリック関数 */
	void USB_Host_Init_Sequence(void);
	void USBH_MainInterrupt(void);
	void USBH_Process(void);
	void TIM3_Init(uint16_t arr, uint16_t psc);

#ifdef __cplusplus
}
#endif

#endif
