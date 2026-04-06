#include "debug.h"
#include "usb_host_gamepad.h"
#include "usbc_source.h"

int main(void) {
  /* 1. コア初期化 */
  NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);
  SystemCoreClockUpdate();
  Delay_Init();

  /* 2. UART初期化: PB10 (Pin 21, デフォルト) 115200bps (ログ出力のため最初に実行) */
  USART_Printf_Init(115200);

  /* 3. USB Type-Cソース初期化 (CCモニタリング + PA0のロードスイッチ) */
  USBC_Source_Init();

  printf("\r\n=== Kachkojir USB Pad ===\r\n");
  
  // __DATE__="Mmm DD YYYY" を "YYYY-MM-DD" に変換
  const char *m = __DATE__;
  int yy = ((m[7]-'0')*1000) + ((m[8]-'0')*100) + ((m[9]-'0')*10) + (m[10]-'0');
  int dd = ((m[4]==' '?'0':m[4])-'0')*10 + (m[5]-'0');
  const char *months[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
  int mm = 0;
  for(int i=0; i<12; i++) if(m[0]==months[i][0] && m[1]==months[i][1] && m[2]==months[i][2]) mm = i+1;
  
  printf("Build: %04d-%02d-%02d %s\r\n", yy, mm, dd, __TIME__);

  /* 4. USBホスト初期化 (USBFS + SPIスレーブ + TIM3) */
  USB_Host_Init_Sequence();

  /* 5. メインループ */
  uint32_t last_cc_check = 0;

  while (1) {
    /* USBホスト ゲームパッド・ポーリング (毎ループ実行) */
    USBH_Process();

    /* CC検出はTIM3の1msティックを使用して約10ms間隔に制限 */
    if ((Current_System_Time - last_cc_check) >= 10) {
      last_cc_check = Current_System_Time;
      USBC_Source_Detect();
    }
  }
}
