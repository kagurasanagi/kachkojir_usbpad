/********************************** (C) COPYRIGHT *******************************
 * File Name          : usbc_source.h
 * Description        : USB Type-C Source mode - CC monitoring and load switch.
 *******************************************************************************/

#ifndef __USBC_SOURCE_H
#define __USBC_SOURCE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ch32x035.h"
#include "ch32x035_usbpd.h"

/* ---------- Configuration ---------- */
/* Load Switch Control Pin (High = ON, Low = OFF) */
#define LOADSW_GPIO_PORT      GPIOA
#define LOADSW_GPIO_PIN       GPIO_Pin_0      /* PA0 (Pin 5), doubles as LED */
#define LOADSW_GPIO_CLK       RCC_APB2Periph_GPIOA

/* Debounce: consecutive stable readings needed (x detection period) */
#define CC_DEBOUNCE_ATTACH    12               /* 10ms x 12 = 120ms (接続規格: 100〜200msを満たす) */
#define CC_DEBOUNCE_DETACH    2               /* 10ms x 2  =  20ms (切断規格: 10〜20ms程度を満たす) */

/* ---------- State ---------- */
typedef enum {
    USBC_SRC_DISCONNECTED = 0,
    USBC_SRC_ATTACHED,                        /* Sink (Rd) detected */
} USBC_SRC_State_t;

/* ---------- Public API ---------- */
void USBC_Source_Init(void);
void USBC_Source_Detect(void);                /* Call periodically (~4ms) */
USBC_SRC_State_t USBC_Source_GetState(void);
uint8_t USBC_Source_GetActiveCC(void);        /* 0: none, 1: CC1, 2: CC2 */

#ifdef __cplusplus
}
#endif

#endif /* __USBC_SOURCE_H */
