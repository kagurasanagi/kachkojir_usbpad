/********************************** (C) COPYRIGHT *******************************
 * ファイル名          : usbc_source.h
 * 説明                : USB Type-C ソースモード - CC モニタリング と ロードスイッチ。
 *******************************************************************************/

#ifndef __USBC_SOURCE_H
#define __USBC_SOURCE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ch32x035.h"
#include "ch32x035_usbpd.h"

/* ---------- コンフィギュレーション ---------- */
/* ロードスイッチ 制御 ピン (Low = ON, High = OFF) - /EN (Active LOW) */
#define LOADSW_GPIO_PORT      GPIOA
#define LOADSW_GPIO_PIN       GPIO_Pin_1      /* PA1 (Pin 6), LED としても機能 */
#define LOADSW_GPIO_CLK       RCC_APB2Periph_GPIOA

/* 過電流検知 (/OC) ピン (Active LOW) */
#define OC_GPIO_PORT          GPIOA
#define OC_GPIO_PIN           GPIO_Pin_0      /* PA0 (Pin 5), 入力 (外部プルアップあり) */
#define OC_GPIO_CLK           RCC_APB2Periph_GPIOA
#define OC_GPIO_ACTIVE        LEVEL_LOW

/* 過電流検知異常 表示 LED (Active HIGH) */
#define FAULT_LED_GPIO_PORT   GPIOC
#define FAULT_LED_GPIO_PIN    GPIO_Pin_3
#define FAULT_LED_GPIO_CLK    RCC_APB2Periph_GPIOC

/* デバウンス: 連続した安定した読み取りが必要 (x 検出 周期) */
#define CC_DEBOUNCE_ATTACH    12               /* 10ms x 12 = 120ms (接続規格: 100〜200msを満たす) */
#define CC_DEBOUNCE_DETACH    2               /* 10ms x 2  =  20ms (切断規格: 10〜20ms程度を満たす) */

/* ---------- ステート ---------- */
typedef enum {
    USBC_SRC_DISCONNECTED = 0,
    USBC_SRC_ATTACHED,                        /* シンク (Rd) を 検出 */
} USBC_SRC_State_t;

/* ---------- パブリック API ---------- */
void USBC_Source_Init(void);
void USBC_Source_Detect(void);                /* 周期的に呼び出し (約 4ms) */
void USBC_Source_HandleOC(void);              /* 割り込みハンドラから呼び出し */
uint8_t USBC_Source_IsFault(void);            /* 1 = 過電流ラッチ状態 */
USBC_SRC_State_t USBC_Source_GetState(void);
uint8_t USBC_Source_GetActiveCC(void);        /* 0: なし, 1: CC1, 2: CC2 */

#ifdef __cplusplus
}
#endif

#endif /* __USBC_SOURCE_H */
