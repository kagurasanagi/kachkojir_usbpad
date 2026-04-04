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

/* SPI Slave Buffer Size */
#define SPI_BUFF_SIZE    3

/* External Variables */
extern uint8_t SPI_Tx_Buf[SPI_BUFF_SIZE];
extern uint8_t SPI_Rx_Buf[SPI_BUFF_SIZE];

/* Public Functions */
void SPI1_Slave_Init(void);
void SPI1_DMA_Init(void);
void SPI1_Update_Data(uint8_t *data);

#ifdef __cplusplus
}
#endif

#endif
