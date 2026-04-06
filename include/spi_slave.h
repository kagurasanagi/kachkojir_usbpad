/********************************** (C) COPYRIGHT *******************************
 * ファイル名          : spi_slave.h
 * 著者                : Antigravity
 * バージョン          : V1.0.0
 * 日付                : 2026/04/04
 * 説明                : SPI スレーブ 通信 ヘッダー。
 *******************************************************************************/

#ifndef __SPI_SLAVE_H
#define __SPI_SLAVE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ch32x035.h"

/* SPI スレーブ バッファ サイズ (HID レポート に十分なサイズ) */
#define SPI_BUFF_SIZE    64

/* SPI コマンド ID (v1.9) */
#define CMD_GET_PAD_INPUT       0x01
#define CMD_GET_PAD_RAW         0x02  // 復活: USB生レポート取得
#define CMD_GET_PAD_RAW_LEN     0x03  // 復活: USB生レポート長取得
#define CMD_GET_SYS_STATUS      0x10
#define CMD_GET_DEVICE_ID       0x11
#define CMD_GET_FW_VERSION      0xFE

/* usb_host_gamepad.c からの外部共有変数 */
extern uint8_t  Gamepad_Status;
extern uint16_t Gamepad_VID;
extern uint16_t Gamepad_PID;
extern volatile uint8_t Gamepad_Stable_Idx;
extern uint8_t Gamepad_SPI_Data[2][3];
extern uint8_t Gamepad_Raw_Report[2][64];
extern uint8_t Gamepad_Raw_Report_Len[2];

/* パブリック関数 */
void SPI1_Slave_Init(void);
void SPI1_DMA_Init(void);
void SPI1_Update_Data(uint8_t *data);

#ifdef __cplusplus
}
#endif

#endif
