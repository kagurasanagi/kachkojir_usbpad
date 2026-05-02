#include "debug.h"
#include "usb_host_gamepad.h"
#include "usbc_source.h"

int main(void)
{
	/* 1. コア初期化 */
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);
	SystemCoreClockUpdate();
	Delay_Init();

	/* 2. UART初期化: PB10 (Pin 21, デフォルト) 115200bps (ログ出力のため最初に実行) */
	USART_Printf_Init(115200);

	/* 3. USB Type-Cソース初期化 (CCモニタリング + PA0のロードスイッチ) */
	USBC_Source_Init();

	/* 4. Independent Watchdog (IWDG) Initialization (~2.0 seconds) */
	IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);
	IWDG_SetPrescaler(IWDG_Prescaler_128); /* 32kHz / 128 = 250Hz (4ms per tick) */
	IWDG_SetReload(500);                   /* 500 * 4ms = 2000ms (2.0s timeout) */
	IWDG_ReloadCounter();
	IWDG_Enable();

	printf("\r\n=== Kachkojir USB Pad ===\r\n");
	print_build_info();

	/* 4. USBホスト初期化 (USBFS + SPIスレーブ + TIM3) */
	USB_Host_Init_Sequence();

	/* 5. メインループ */
	uint32_t last_cc_check = 0;

	while (1)
	{
		/* USBホスト ゲームパッド・ポーリング (毎ループ実行) */
		USBH_Process();

		/* CC検出はTIM3の1msティックを使用して約10ms間隔に制限 */
		if ((Current_System_Time - last_cc_check) >= 10)
		{
			last_cc_check = Current_System_Time;
			USBC_Source_Detect();
		}

		/* IWDGをクリアしてリセットを防止 */
		IWDG_ReloadCounter();
	}
}
