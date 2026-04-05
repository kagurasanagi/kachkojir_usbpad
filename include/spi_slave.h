/********************************** (C) COPYRIGHT *******************************
 * File Name          : spi_slave.h
 * Author             : Antigravity
 * Version            : V1.0.0
 * Date               : 2026/04/04
 * Description        : SPI Slave communication header.
 *******************************************************************************/

#ifndef __SPI_SLAVE_H
#define __SPI_SLAVE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ch32x035.h"

/* SPI Slave Buffer Size (Enough for HID report) */
#define SPI_BUFF_SIZE    64

/* SPI Command IDs (v1.9) */
#define CMD_GET_PAD_INPUT       0x01
#define CMD_GET_PAD_RAW         0x02
#define CMD_GET_PAD_RAW_LEN     0x03
#define CMD_GET_SYS_STATUS      0x10
#define CMD_GET_DEVICE_ID       0x11
#define CMD_GET_FW_VERSION      0xFE

/* External Variables from usb_host_gamepad.c */
extern uint8_t  Gamepad_Status;
extern uint16_t Gamepad_VID;
extern uint16_t Gamepad_PID;
extern uint8_t  Gamepad_Raw_Len;
extern uint8_t  Gamepad_Report_Buf[64];
extern uint8_t  Gamepad_SPI_Final[3];

/* Public Functions */
void SPI1_Slave_Init(void);
void SPI1_DMA_Init(void);
void SPI1_Update_Data(uint8_t *data);

#ifdef __cplusplus
}
#endif

#endif
