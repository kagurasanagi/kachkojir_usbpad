#include "debug.h"
#include "usb_host_gamepad.h"
#include "usbc_source.h"

int main(void) {
  /* 1. Core Init */
  NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);
  SystemCoreClockUpdate();
  Delay_Init();

  /* 2. UART Init: PB10 (Pin 21, Default) 115200bps */
  USART_Printf_Init(115200);
  printf("\r\n=== Kachkojir USB Pad ===\r\n");

  /* 3. USB Host Init (USBFS + SPI Slave + TIM3) */
  USB_Host_Init_Sequence();

  /* 4. USB Type-C Source Init (CC monitoring + Load Switch on PA0) */
  USBC_Source_Init();

  /* 5. Main Loop */
  uint32_t last_cc_check = 0;

  while (1) {
    /* USB Host gamepad polling (runs every iteration) */
    USBH_Process();

    /* CC detection throttled to ~100ms intervals using TIM3's 1ms tick */
    if ((Current_System_Time - last_cc_check) >= 100) {
      last_cc_check = Current_System_Time;
      USBC_Source_Detect();
    }
  }
}
