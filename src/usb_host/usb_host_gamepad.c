#include "usb_host_gamepad.h"

#include <string.h>

#include "ch32x035_gpio.h"
#include "ch32x035_pwr.h"
#include "ch32x035_tim.h"
#include "ch32x035_usbfs_host.h"
#include "debug.h"
#include "pin_config.h"
#include "spi_slave.h"
#include "usb_host_config.h"

/*******************************************************************************/
/* 変数定義 */
uint8_t DevDesc_Buf[18];
uint8_t Com_Buf[DEF_COM_BUF_LEN];
struct _ROOT_HUB_DEVICE RootHubDev;
struct __HOST_CTL HostCtl[DEF_TOTAL_ROOT_HUB * DEF_ONE_USB_SUP_DEV_TOTAL];

uint8_t Gamepad_Status = GAMEPAD_DISCONNECT;
uint8_t Gamepad_Report_Buf[64];
uint8_t Gamepad_Prev_Report_Buf[64];
uint8_t Gamepad_SPI_Prev[3] = {0};			 // 24ビットバイナリ変更検出用
static uint8_t Gamepad_Comm_Ready = 0;		 // 1 = HOMEボタン後の通信応答中
static uint32_t last_usb_response_time = 0;	 // 有効なPID応答があった最後のシステムタイム
static uint32_t Gamepad_Data_Last_Time = 0;	 // 有効なデータパケット(ERR_SUCCESS)が到着した最後の時間

/* Shared buffers (Double buffering) */
volatile uint8_t Gamepad_Stable_Idx = 0;  // 0 or 1. Index SPI master can safely read.
uint8_t Gamepad_SPI_Data[2][3] = {{0}, {0}};
uint8_t Gamepad_Raw_Report[2][64] = {{0}, {0}};
uint8_t Gamepad_Raw_Report_Len[2] = {0, 0};
uint32_t Current_System_Time = 0;  // Incremented by TIM3

static const GamepadProfile_t *current_profile = NULL;

uint16_t Gamepad_VID = 0;
uint16_t Gamepad_PID = 0;
uint8_t Gamepad_Raw_Len = 0;
uint8_t Gamepad_Is_Switch_Clone = 0;	// 偽物Switchコントローラーのフラグ



/**
 * @brief   Initializes the USBFS host, timers, and related peripherals.
 *          Wait for enumeration to complete after setup.
 * @return  None
 */
void USB_Host_Init_Sequence(void)
{
	/* USBFSホストを初期化 */
	printf("USBFS Host Init (Supply=%dV)\r\n", (PWR_VDD_SupplyVoltage() == PWR_VDD_5V) ? 5 : 3);
	USBFS_RCC_Init();
	USBFS_Host_Init(ENABLE, PWR_VDD_SupplyVoltage());
	memset(&RootHubDev, 0, sizeof(RootHubDev));
	memset(HostCtl, 0, sizeof(HOST_CTL) * DEF_TOTAL_ROOT_HUB * DEF_ONE_USB_SUP_DEV_TOTAL);

	/* LED初期化: PA2(PadEnable),PA3(ステータス)
	 * GPIOPA_CFGLR (0x40010800): PA2ビット11-8,PA3ビット15-12
	 * モード: 11(出力50MHz),CNF:00(プッシュプル)->0x3
	 */
	/* LED初期化: Ready, Status */
	{
		GPIO_InitTypeDef GPIO_InitStructure = {0};
		RCC_APB2PeriphClockCmd(USB_READY_LED_CLK | USB_STATUS_LED_CLK, ENABLE);
		GPIO_InitStructure.GPIO_Pin = USB_READY_LED_PIN | USB_STATUS_LED_PIN;
		GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
		GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
		GPIO_Init(GPIOA, &GPIO_InitStructure);
		GPIO_WriteBit(USB_READY_LED_PORT, USB_READY_LED_PIN, Bit_RESET);
		GPIO_WriteBit(USB_STATUS_LED_PORT, USB_STATUS_LED_PIN, Bit_RESET);
	}

	/* デバッグ用ピン初期化: 15番ピン(PB4)を内部プルダウン */
	{
		GPIO_InitTypeDef GPIO_InitStructure = {0};
		RCC_APB2PeriphClockCmd(DEBUG_DUMP_GPIO_CLK, ENABLE);
		GPIO_InitStructure.GPIO_Pin = DEBUG_DUMP_GPIO_PIN;
		GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPD;
		GPIO_Init(DEBUG_DUMP_GPIO_PORT, &GPIO_InitStructure);
	}

	/* RP2350通信用のSPIスレーブを初期化 */
	SPI1_Slave_Init();
	SPI1_DMA_Init();

	/* 1msティック用のタイマーを初期化 */
	TIM3_Init(9, SystemCoreClock / 10000 - 1);

	if (SystemCoreClock != 48000000)
	{
		printf("WARNING: SystemCoreClock is NOT 48MHz! USBFS may fail.\r\n");
	}
	printf("Waiting for gamepad...\r\n");
}

/*******************************************************************************
 * @fn      TIM3_Init
 * @brief   ポーリングインターバルカウント用のタイマー3を初期化します。
 */
void TIM3_Init(uint16_t arr, uint16_t psc)
{
	TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure = {0};
	NVIC_InitTypeDef NVIC_InitStructure = {0};

	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);

	TIM_TimeBaseStructure.TIM_Period = arr;
	TIM_TimeBaseStructure.TIM_Prescaler = psc;
	TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
	TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
	TIM_TimeBaseInit(TIM3, &TIM_TimeBaseStructure);

	TIM_ITConfig(TIM3, TIM_IT_Update, ENABLE);

	NVIC_InitStructure.NVIC_IRQChannel = TIM3_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;  // 優先度を下げた
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 2;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&NVIC_InitStructure);

	TIM_Cmd(TIM3, ENABLE);
}

/*******************************************************************************
 * @fn      TIM3_IRQHandler
 * @brief   USBエンドポイントの転送ディレイロジック。
 */
void TIM3_IRQHandler(void) __attribute__((interrupt));
void TIM3_IRQHandler(void)
{
	uint8_t intf, in_num;
	if (TIM_GetITStatus(TIM3, TIM_IT_Update) != RESET)
	{
		TIM_ClearITPendingBit(TIM3, TIM_IT_Update);
		Current_System_Time++;	// 1msティック
		if (Gamepad_Status >= GAMEPAD_ENUMERATED)
		{
			for (intf = 0; intf < HostCtl[0].InterfaceNum; intf++)
			{
				for (in_num = 0; in_num < HostCtl[0].Interface[intf].InEndpNum; in_num++)
				{
					HostCtl[0].Interface[intf].InEndpTimeCount[in_num]++;
				}
			}
		}
	}
}

/*******************************************************************************
 * @fn      USBH_AnalyseType
 */
void USBH_AnalyseType(uint8_t *pdev_buf, uint8_t *pcfg_buf, uint8_t *ptype)
{
	uint8_t dv_cls, if_cls;
	dv_cls = ((PUSB_DEV_DESCR)pdev_buf)->bDeviceClass;
	if_cls = ((PUSB_CFG_DESCR_LONG)pcfg_buf)->itf_descr.bInterfaceClass;

	if (dv_cls == USB_DEV_CLASS_HID || if_cls == USB_DEV_CLASS_HID)
	{
		*ptype = USB_DEV_CLASS_HID;
	}
	else
	{
		*ptype = DEF_DEV_TYPE_UNKNOWN;
	}
}

/*******************************************************************************
 * @fn      LogUSBString
 * @brief   Prints a USB string descriptor as ASCII.
 */
static void LogUSBString(uint8_t ep0_size, uint8_t index, const char *label)
{
	printf("%-18s: ", label);
	if (index == 0)
	{
		printf("(None)\r\n");
		return;
	}
	uint8_t buf[64];
	if (USBFSH_GetStrDescr(ep0_size, index, buf) == ERR_SUCCESS)
	{
		if (buf[0] > 2)
		{
			for (int i = 2; i < buf[0]; i += 2)
			{
				printf("%c", buf[i]);
			}
			printf("\r\n");
		}
		else
		{
			printf("(Empty)\r\n");
		}
	}
	else
	{
		printf("(Read Error)\r\n");
	}
}

/*******************************************************************************
 * @fn      CheckSwitchCloneAnomaly
 * @brief   製造元が空、製品名が "Gamepad" の場合にフラグをセットする
 */
static void CheckSwitchCloneAnomaly(uint8_t ep0_size, uint8_t iManufacturer, uint8_t iProduct)
{
	uint8_t buf[64];
	Gamepad_Is_Switch_Clone = 0;

	/* 製造元が空であることを確認 */
	if (iManufacturer != 0)
		return;

	/* 製品名を取得して "Gamepad" か確認 */
	if (iProduct != 0 && USBFSH_GetStrDescr(ep0_size, iProduct, buf) == ERR_SUCCESS)
	{
		/* "Gamepad" は UTF-16 で 'G',0,'a',0,'m',0,'e',0,'p',0,'a',0,'d',0 (14 bytes characters)
		   Descriptor header は 2 bytes なので合計 16 bytes */
		if (buf[0] == 16 &&
			buf[2] == 'G' && buf[4] == 'a' && buf[6] == 'm' && buf[8] == 'e' &&
			buf[10] == 'p' && buf[12] == 'a' && buf[14] == 'd')
		{
			Gamepad_Is_Switch_Clone = 1;
			printf("Detected possible Switch Controller clone (anomaly mitigation enabled)\r\n");
		}
	}
}

/*******************************************************************************
 * @fn      USBH_EnumRootDevice
 */
uint8_t USBH_EnumRootDevice(void)
{
	uint8_t s, enum_cnt, cfg_val;
	uint16_t i, len;

	enum_cnt = 0;
ENUM_START:
	Delay_Ms(100);
	enum_cnt++;
	if (enum_cnt > 3)
		return ERR_USB_UNSUPPORT;

	USBFSH_ResetRootHubPort(0);
	for (i = 0, s = 0; i < DEF_RE_ATTACH_TIMEOUT; i++)
	{
		if (USBFSH_EnableRootHubPort(&RootHubDev.bSpeed) == ERR_SUCCESS)
		{
			s++;
			if (s > 10)
				break;
		}
		Delay_Ms(1);
	}
	if (i >= DEF_RE_ATTACH_TIMEOUT)
		goto ENUM_START;

	s = USBFSH_GetDeviceDescr(&RootHubDev.bEp0MaxPks, DevDesc_Buf);
	if (s != ERR_SUCCESS)
		goto ENUM_START;

	/* VIDとPIDを抽出 */
	Gamepad_VID = DevDesc_Buf[8] | ((uint16_t)DevDesc_Buf[9] << 8);
	Gamepad_PID = DevDesc_Buf[10] | ((uint16_t)DevDesc_Buf[11] << 8);

	RootHubDev.bAddress = (uint8_t)(USB_DEVICE_ADDR);
	s = USBFSH_SetUsbAddress(RootHubDev.bEp0MaxPks, RootHubDev.bAddress);
	if (s != ERR_SUCCESS)
		goto ENUM_START;
	Delay_Ms(5);

	/* Read string descriptors (Manufacturer, Product, and SerialNumber) */
	LogUSBString(RootHubDev.bEp0MaxPks, DevDesc_Buf[14], "Manufacturer");
	LogUSBString(RootHubDev.bEp0MaxPks, DevDesc_Buf[15], "Product");
	LogUSBString(RootHubDev.bEp0MaxPks, DevDesc_Buf[16], "SerialNumber");

	/* 偽物コントローラーの判定 */
	CheckSwitchCloneAnomaly(RootHubDev.bEp0MaxPks, DevDesc_Buf[14], DevDesc_Buf[15]);

	s = USBFSH_GetConfigDescr(RootHubDev.bEp0MaxPks, Com_Buf, DEF_COM_BUF_LEN, &len);
	if (s == ERR_SUCCESS)
	{
		cfg_val = ((PUSB_CFG_DESCR)Com_Buf)->bConfigurationValue;
		USBH_AnalyseType(DevDesc_Buf, Com_Buf, &RootHubDev.bType);
	}
	else
		goto ENUM_START;

	s = USBFSH_SetUsbConfig(RootHubDev.bEp0MaxPks, cfg_val);
	if (s != ERR_SUCCESS)
		return ERR_USB_UNSUPPORT;

	return ERR_SUCCESS;
}

/*******************************************************************************
 * @fn      GAMEPAD_AnalyzeConfigDesc
 */
uint8_t GAMEPAD_AnalyzeConfigDesc(uint8_t index, uint8_t ep0_size)
{
	uint16_t i, total_len;
	uint8_t num = 0, innum = 0;

	total_len = (Com_Buf[2] + ((uint16_t)Com_Buf[3] << 8));
	for (i = 0; i < total_len;)
	{
		if (Com_Buf[i + 1] == DEF_DECR_CONFIG)
		{
			HostCtl[index].InterfaceNum = ((PUSB_CFG_DESCR)(&Com_Buf[i]))->bNumInterfaces;
			i += Com_Buf[i];
		}
		else if (Com_Buf[i + 1] == DEF_DECR_INTERFACE)
		{
			if (((PUSB_ITF_DESCR)(&Com_Buf[i]))->bInterfaceClass == 0x03)  // HID
			{
				HostCtl[index].Interface[num].Type = 0xFF;
				i += Com_Buf[i];
				innum = 0;
				while (i < total_len && Com_Buf[i + 1] != DEF_DECR_INTERFACE)
				{
					if (Com_Buf[i + 1] == DEF_DECR_ENDPOINT)
					{
						if (((PUSB_ENDP_DESCR)(&Com_Buf[i]))->bEndpointAddress & 0x80)
						{
							HostCtl[index].Interface[num].InEndpAddr[innum] =
								((PUSB_ENDP_DESCR)(&Com_Buf[i]))->bEndpointAddress & 0x7F;
							HostCtl[index].Interface[num].InEndpSize[innum] =
								((PUSB_ENDP_DESCR)(&Com_Buf[i]))->wMaxPacketSizeL;
							HostCtl[index].Interface[num].InEndpInterval[innum] =
								((PUSB_ENDP_DESCR)(&Com_Buf[i]))->bInterval;
							HostCtl[index].Interface[num].InEndpNum++;
							innum++;
						}
					}
					i += Com_Buf[i];
				}
			}
			else
				i += Com_Buf[i];
			num++;
		}
		else
			i += Com_Buf[i];
	}
	return ERR_SUCCESS;
}

/* The Gamepad_Data_Map and hat_map logic has been moved to gamepad_mapper.c */

/**
 * @brief   Main USB host state machine processor.
 *          Should be called frequently in the main loop to handle polling and device status.
 * @return  None
 */
void USBH_Process(void)
{
	uint8_t s, intf, in_num;
	uint16_t len;
	static uint8_t last_status = ROOT_DEV_DISCONNECT;

	s = USBFSH_CheckRootHubPortStatus(last_status);
	if (s != last_status)
	{
		if (s == ROOT_DEV_CONNECTED)
		{
			printf("Device connected\r\n");
			uint8_t enum_res = USBH_EnumRootDevice();
			if (enum_res == ERR_SUCCESS)
			{
				printf("Enumeration success: VID=%04X, PID=%04X\r\n", Gamepad_VID, Gamepad_PID);
				if (RootHubDev.bType == USB_DEV_CLASS_HID)
				{
					GAMEPAD_AnalyzeConfigDesc(0, RootHubDev.bEp0MaxPks);
					current_profile = GamepadMapper_FindProfile(Gamepad_VID, Gamepad_PID);
					Gamepad_Status = GAMEPAD_ENUMERATED;
					printf("Gamepad OK (VID=%04X, PID=%04X, Interfaces=%d)\r\n", Gamepad_VID, Gamepad_PID, HostCtl[0].InterfaceNum);
				}
				else
				{
					printf("Not HID: Type=%d\r\n", RootHubDev.bType);
				}
			}
			else
			{
				printf("Enumeration failed: %02x\r\n", enum_res);
			}
		}
		else if (s == ROOT_DEV_DISCONNECT)
		{
			printf("Device disconnected\r\n");
			Gamepad_Status = GAMEPAD_DISCONNECT;
			memset(&HostCtl, 0, sizeof(HostCtl));
			memset(Gamepad_SPI_Data, 0, sizeof(Gamepad_SPI_Data));
			memset(Gamepad_Raw_Report, 0, sizeof(Gamepad_Raw_Report));
			memset(Gamepad_Raw_Report_Len, 0, sizeof(Gamepad_Raw_Report_Len));
		}
		else if (s == ROOT_DEV_FAILED)
		{
			/* DISCONNECTにリセットし、ステートマシンが再検出できるようにします
			 */
			s = ROOT_DEV_DISCONNECT;
		}
		last_status = s;
	}

	if (Gamepad_Status == GAMEPAD_ENUMERATED)
	{
		for (intf = 0; intf < HostCtl[0].InterfaceNum; intf++)
		{
			for (in_num = 0; in_num < HostCtl[0].Interface[intf].InEndpNum; in_num++)
			{
				if (HostCtl[0].Interface[intf].InEndpTimeCount[in_num] >=
					HostCtl[0].Interface[intf].InEndpInterval[in_num])
				{
					HostCtl[0].Interface[intf].InEndpTimeCount[in_num] = 0;
					len = HostCtl[0].Interface[intf].InEndpSize[in_num];
					s = USBFSH_GetEndpData(HostCtl[0].Interface[intf].InEndpAddr[in_num],
										   &HostCtl[0].Interface[intf].InEndpTog[in_num], Gamepad_Report_Buf, &len);

					/* --- 100ms安定ディレイを伴うレジスタベースのPA1更新 --- */
					if (USBFSH->MIS_ST & USBFS_UMS_DEV_ATTACH)
					{
						uint8_t res = USBFSH->INT_ST & 0x0F;
						if (res != 0x00)
						{
							last_usb_response_time = Current_System_Time; /* 応答OK: 成功時間を記録 */
						}
					}

					if (s == ERR_SUCCESS && len > 0 && current_profile != NULL)
					{
						uint8_t write_idx = 1 - Gamepad_Stable_Idx;

						/* Clear the output buffer before processing */
						memset(Gamepad_SPI_Data[write_idx], 0, 3);

						/* Call the profile-specific parsing logic */
						if (current_profile->process(Gamepad_Report_Buf, len, Gamepad_SPI_Data[write_idx]) == REPORT_HANDLED)
						{
							/* Save the raw report for debugging/passthrough */
							uint16_t copy_len = (len > 64) ? 64 : len;
							memcpy(Gamepad_Raw_Report[write_idx], Gamepad_Report_Buf, copy_len);
							Gamepad_Raw_Report_Len[write_idx] = (uint8_t)copy_len;

							/* ★ Atomic switch of the stable index to prevent read-tearing by SPI DMA ★ */
							__disable_irq();
							Gamepad_Stable_Idx = write_idx;
							__enable_irq();

							/* Tell the host loop we've updated data */
							Gamepad_Data_Last_Time = Current_System_Time;
							Gamepad_Raw_Len = (uint8_t)len;

							/* Optional: Update logs if report changed */
							if (memcmp(Gamepad_Report_Buf, Gamepad_Prev_Report_Buf, len) != 0)
							{
								memcpy(Gamepad_Prev_Report_Buf, Gamepad_Report_Buf, len);

								/* 15番ピンがHighならレポートをダンプ */
								if (GPIO_ReadInputDataBit(DEBUG_DUMP_GPIO_PORT, DEBUG_DUMP_GPIO_PIN) == Bit_SET)
								{
									printf("RAW (len=%d): ", len);
									for (uint16_t i = 0; i < len; i++)
									{
										printf("%02X ", Gamepad_Report_Buf[i]);
									}
									printf("\r\n");
								}
							}
						}
					}
				}
			}
		}
	}

	/* --- 常時物理的切断チェック --- */
	if (!(USBFSH->MIS_ST & USBFS_UMS_DEV_ATTACH))
	{
		/* 物理的に切断(R8_MIS_STビット0 == 0) */
		last_usb_response_time = Current_System_Time - 1000;
		Gamepad_Comm_Ready = 0;
		memset(Gamepad_SPI_Data, 0, sizeof(Gamepad_SPI_Data));
		memset(Gamepad_Raw_Report, 0, sizeof(Gamepad_Raw_Report));
		memset(Gamepad_Raw_Report_Len, 0, sizeof(Gamepad_Raw_Report_Len));
		GPIO_WriteBit(USB_READY_LED_PORT, USB_READY_LED_PIN, Bit_RESET);
		GPIO_WriteBit(USB_STATUS_LED_PORT, USB_STATUS_LED_PIN, Bit_RESET);
		Gamepad_Status = GAMEPAD_DISCONNECT;
		Gamepad_Data_Last_Time = 0;
	}

	/* 有効な応答の100msタイムアウトに基づいてGamepad_Comm_Readyを更新 */
	if (Gamepad_Status == GAMEPAD_ENUMERATED)
	{
		Gamepad_Comm_Ready = ((Current_System_Time - last_usb_response_time) < 100);
	}
	else
	{
		Gamepad_Comm_Ready = 0;
	}

	/* USB_READY_LED:通信レディステータスに基づく */
	GPIO_WriteBit(USB_READY_LED_PORT, USB_READY_LED_PIN, (Gamepad_Comm_Ready) ? Bit_SET : Bit_RESET);

	/* USB_STATUS_LED: Always update if data is fresh (within 100ms) and any
	 * non-neutral bit is set */
	uint8_t data_is_fresh = ((Current_System_Time - Gamepad_Data_Last_Time) < 100);
	uint8_t *spi_data = Gamepad_SPI_Data[Gamepad_Stable_Idx];
	if (data_is_fresh && (spi_data[0] != 0x00 || spi_data[1] != JOYSTICK_NEUTRAL_VAL_BYTE1 || spi_data[2] != JOYSTICK_NEUTRAL_VAL_BYTE2))
	{
		GPIO_WriteBit(USB_STATUS_LED_PORT, USB_STATUS_LED_PIN, Bit_SET);
	}
	else
	{
		GPIO_WriteBit(USB_STATUS_LED_PORT, USB_STATUS_LED_PIN, Bit_RESET);
	}
}
