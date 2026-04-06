#include "debug.h"
#include "usb_host_gamepad.h"
#include "usbc_source.h"

int main(void) {
  /* 1. Core Init */
  NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);
  SystemCoreClockUpdate();
  Delay_Init();

  /* 2. UART Init: PB10 (Pin 21, Default) 115200bps (Must be first for logs) */
  USART_Printf_Init(115200);

  /* 3. USB Type-C Source Init (CC monitoring + Load Switch on PA0) */
  USBC_Source_Init();

  printf("\r\n=== Kachkojir USB Pad ===\r\n");
  
  // Format __DATE__="Mmm DD YYYY" to "YYYY-MM-DD"
  const char *m = __DATE__;
  int yy = ((m[7]-'0')*1000) + ((m[8]-'0')*100) + ((m[9]-'0')*10) + (m[10]-'0');
  int dd = ((m[4]==' '?'0':m[4])-'0')*10 + (m[5]-'0');
  const char *months[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
  int mm = 0;
  for(int i=0; i<12; i++) if(m[0]==months[i][0] && m[1]==months[i][1] && m[2]==months[i][2]) mm = i+1;
  
  printf("Build: %04d-%02d-%02d %s\r\n", yy, mm, dd, __TIME__);

  /* 4. USB Host Init (USBFS + SPI Slave + TIM3) */
  USB_Host_Init_Sequence();

  /* 5. Main Loop */
  uint32_t last_cc_check = 0;

  while (1) {
    /* USB Host gamepad polling (runs every iteration) */
    USBH_Process();

    /* CC detection throttled to ~10ms intervals using TIM3's 1ms tick */
    if ((Current_System_Time - last_cc_check) >= 10) {
      last_cc_check = Current_System_Time;
      USBC_Source_Detect();
    }
  }
}
