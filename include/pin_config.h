/********************************** (C) COPYRIGHT
 ******************************** ファイル名          : pin_config.h 説明 :
 *各ピンの割当て設定。
 *******************************************************************************/

#ifndef __PIN_CONFIG_H
#define __PIN_CONFIG_H

#include "ch32x035.h"

/* ---------- 電源制御 (USB Type-C) ---------- */

/* ロードスイッチ 制御 ピン - /EN (Active LOW): Low=ON, High=OFF
 * ※ PA1 (Pin 6)。基板上の LED1 としても機能 */
#define LOADSW_GPIO_PORT GPIOA
#define LOADSW_GPIO_PIN GPIO_Pin_1
#define LOADSW_GPIO_CLK RCC_APB2Periph_GPIOA

#define LOADSW_ON  Bit_RESET  /* /EN: Low = スイッチON */
#define LOADSW_OFF Bit_SET    /* /EN: High = スイッチOFF */

/* 過電流検知 (/OC) ピン - Active LOW: Low=障害発生 */
#define OC_GPIO_PORT GPIOA
#define OC_GPIO_PIN GPIO_Pin_0
#define OC_GPIO_CLK RCC_APB2Periph_GPIOA

#define OC_ON  Bit_RESET           /* /OC: Low = 過電流発生 */
#define OC_OFF Bit_SET             /* /OC: High = 正常 */
#define OC_EXTI_TRIGGER EXTI_Trigger_Falling  /* /OC: Low になるとき (Falling) で割り込み */

/* 過電流検知異常 表示 LED (Active HIGH) */
#define FAULT_LED_GPIO_PORT GPIOC
#define FAULT_LED_GPIO_PIN GPIO_Pin_3
#define FAULT_LED_GPIO_CLK RCC_APB2Periph_GPIOC

/* USB ホスト ステータス LED (PadEnable) */
#define USB_READY_LED_PORT GPIOA
#define USB_READY_LED_PIN GPIO_Pin_2
#define USB_READY_LED_CLK RCC_APB2Periph_GPIOA

/* USB ホスト 通信中 LED (Status) */
#define USB_STATUS_LED_PORT GPIOA
#define USB_STATUS_LED_PIN GPIO_Pin_3
#define USB_STATUS_LED_CLK RCC_APB2Periph_GPIOA

/* ---------- SPIスレーブ (RP2350通信用) ---------- */

#define SPI1_NSS_GPIO_PORT GPIOA
#define SPI1_NSS_GPIO_PIN GPIO_Pin_4
#define SPI1_NSS_GPIO_CLK RCC_APB2Periph_GPIOA
#define SPI1_NSS_EXTI_LINE EXTI_Line4
#define SPI1_NSS_PORT_SOURCE GPIO_PortSourceGPIOA
#define SPI1_NSS_PIN_SOURCE GPIO_PinSource4

#define SPI1_SCK_GPIO_PORT GPIOA
#define SPI1_SCK_GPIO_PIN GPIO_Pin_5
#define SPI1_SCK_GPIO_CLK RCC_APB2Periph_GPIOA

#define SPI1_MISO_GPIO_PORT GPIOA
#define SPI1_MISO_GPIO_PIN GPIO_Pin_6
#define SPI1_MISO_GPIO_CLK RCC_APB2Periph_GPIOA

#define SPI1_MOSI_GPIO_PORT GPIOA
#define SPI1_MOSI_GPIO_PIN GPIO_Pin_7
#define SPI1_MOSI_GPIO_CLK RCC_APB2Periph_GPIOA

/* ---------- UART デバッグ (USART1) ---------- */

#define UART_DEBUG_PORT GPIOB
#define UART_DEBUG_PIN GPIO_Pin_10
#define UART_DEBUG_CLK RCC_APB2Periph_GPIOB

/* ---------- USB FS ホスト設定 ---------- */

#define USBFS_ID_GPIO_PORT GPIOC
#define USBFS_ID_GPIO_PIN_16 GPIO_Pin_16
#define USBFS_ID_GPIO_PIN_17 GPIO_Pin_17
#define USBFS_ID_GPIO_CLK RCC_APB2Periph_GPIOC

/* ---------- デバッグ用 設定 ---------- */
/* 15番ピン (PB4) をレポートダンプのトリガーに使用 */
#define DEBUG_DUMP_GPIO_PORT GPIOB
#define DEBUG_DUMP_GPIO_PIN GPIO_Pin_4
#define DEBUG_DUMP_GPIO_CLK RCC_APB2Periph_GPIOB

#endif /* __PIN_CONFIG_H */
