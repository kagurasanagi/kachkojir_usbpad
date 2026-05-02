#include "spi_slave.h"

#include <string.h>

#include "usb_host_gamepad.h"
#include "usbc_source.h"

/* Global communication state */
static volatile uint8_t spi_rx_cnt = 0;
static volatile uint8_t spi_curr_cmd = 0;

/* Optimization: DMA-based response for reliable jitter-free SPI */
static uint8_t spi_res_null = 0;
static uint8_t *spi_tx_ptr = &spi_res_null;
static uint8_t spi_tx_len = 0;

/* RMW排除用キャッシュ: 初期化後に一度だけ計算し、ISR内で直接書き込む */
static uint32_t s_dma_cfgr_base;    // ENビットなし
static uint32_t s_dma_cfgr_enable;  // ENビットあり
static uint16_t s_spi_ctlr2_dma_on; // TXDMAEN=1
static uint16_t s_spi_ctlr2_dma_off;// TXDMAEN=0

/* 定数値または構築された値用の静的応答バッファ */
static uint8_t spi_res_status;
static uint8_t spi_res_version[2] = {0x01, 0x09};  // v1.9
static uint8_t spi_res_id[4];

/**
 * @brief   Initializes SPI1 as a slave with EXTI for CS synchronization.
 *          Configures GPIOs, EXTI on NSS (PA4) and OC (PA0), and SPI/DMA.
 * @return  None
 */
void SPI1_Slave_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure = {0};
	SPI_InitTypeDef SPI_InitStructure = {0};
	NVIC_InitTypeDef NVIC_InitStructure = {0};
	EXTI_InitTypeDef EXTI_InitStructure = {0};

	/* クロック有効化: PA, SPI1, AFIO */
	RCC_APB2PeriphClockCmd(SPI1_NSS_GPIO_CLK | RCC_APB2Periph_SPI1 | RCC_APB2Periph_AFIO, ENABLE);

	/* SPI1 GPIO設定: NSS, SCK, MISO, MOSI */
	GPIO_InitStructure.GPIO_Pin = SPI1_SCK_GPIO_PIN | SPI1_MOSI_GPIO_PIN;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
	GPIO_Init(SPI1_SCK_GPIO_PORT, &GPIO_InitStructure);

	GPIO_InitStructure.GPIO_Pin = SPI1_MISO_GPIO_PIN;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(SPI1_MISO_GPIO_PORT, &GPIO_InitStructure);

	/* CS同期用のEXTIとしてNSSを設定（立ち下がりエッジ） */
	GPIO_InitStructure.GPIO_Pin = SPI1_NSS_GPIO_PIN;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;  // 内部プルアップ
	GPIO_Init(SPI1_NSS_GPIO_PORT, &GPIO_InitStructure);

	GPIO_EXTILineConfig(SPI1_NSS_PORT_SOURCE, SPI1_NSS_PIN_SOURCE);
	EXTI_InitStructure.EXTI_Line = SPI1_NSS_EXTI_LINE;
	EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;
	EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising_Falling;	// 立ち下がり時の低負荷化のため両エッジ
	EXTI_InitStructure.EXTI_LineCmd = ENABLE;
	EXTI_Init(&EXTI_InitStructure);

	/* /OC 用の EXTI として設定 */
	GPIO_EXTILineConfig(GPIO_PortSourceGPIOA, GPIO_PinSource0);
	EXTI_InitStructure.EXTI_Line = EXTI_Line0;
	EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;
	EXTI_InitStructure.EXTI_Trigger = OC_EXTI_TRIGGER;
	EXTI_InitStructure.EXTI_LineCmd = ENABLE;
	EXTI_Init(&EXTI_InitStructure);

	/* SPI1 設定 */
	SPI_InitStructure.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
	SPI_InitStructure.SPI_Mode = SPI_Mode_Slave;
	SPI_InitStructure.SPI_DataSize = SPI_DataSize_8b;
	SPI_InitStructure.SPI_CPOL = SPI_CPOL_Low;	  // READMEに従いモード0
	SPI_InitStructure.SPI_CPHA = SPI_CPHA_1Edge;  // READMEに従いモード0
	SPI_InitStructure.SPI_NSS = SPI_NSS_Hard;	  // ハードウェアNSS管理により通信終了時の自動リセットを有効化（EXTIも併用）
	SPI_InitStructure.SPI_FirstBit = SPI_FirstBit_MSB;
	SPI_InitStructure.SPI_CRCPolynomial = 7;
	SPI_Init(SPI1, &SPI_InitStructure);

	/* コマンド解析用の受信割り込みを有効化 */
	SPI_I2S_ITConfig(SPI1, SPI_I2S_IT_RXNE, ENABLE);

	/* SPIおよびEXTI用のNVIC設定 */
	NVIC_InitStructure.NVIC_IRQChannel = SPI1_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;  // 最高
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;		   // 最高
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&NVIC_InitStructure);

	NVIC_InitStructure.NVIC_IRQChannel = EXTI7_0_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;	// CS同期用に高優先度
	NVIC_Init(&NVIC_InitStructure);

	SPI_Cmd(SPI1, ENABLE);

	/* 安定した高速送信のためのDMA初期化 */
	SPI1_DMA_Init();
}

/**
 * @brief   Initializes DMA1 Channel 3 for multi-byte SPI responses.
 *          This allows fast memory-to-peripheral transfers without CPU overhead.
 * @return  None
 */
void SPI1_DMA_Init(void)
{
	DMA_InitTypeDef DMA_InitStructure = {0};

	RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);

	/* SPI1 TX用のDMA1 チャンネル3 */
	DMA_DeInit(DMA1_Channel3);
	DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&SPI1->DATAR;
	DMA_InitStructure.DMA_MemoryBaseAddr = (uint32_t)NULL; /* Configured during command reception */
	DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralDST;
	DMA_InitStructure.DMA_BufferSize = 0;  // コマンド受信時に設定
	DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
	DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
	DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
	DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
	DMA_InitStructure.DMA_Mode = DMA_Mode_Normal;
	DMA_InitStructure.DMA_Priority = DMA_Priority_VeryHigh;
	DMA_InitStructure.DMA_M2M = DMA_M2M_Disable;
	DMA_Init(DMA1_Channel3, &DMA_InitStructure);

	/* RMW排除用: 初期化完了後のCFGR値をキャッシュ */
	s_dma_cfgr_base   = DMA1_Channel3->CFGR & ~DMA_CFGR1_EN;
	s_dma_cfgr_enable = s_dma_cfgr_base | DMA_CFGR1_EN;

	/* RMW排除用: SPI CTLR2のTXDMAEN ON/OFF値をキャッシュ */
	s_spi_ctlr2_dma_off = SPI1->CTLR2 & ~(uint16_t)SPI_I2S_DMAReq_Tx;
	s_spi_ctlr2_dma_on  = s_spi_ctlr2_dma_off | (uint16_t)SPI_I2S_DMAReq_Tx;
}

/**
 * @brief   EXTI Line 7..0 Interrupt Handler.
 *          Handles NSS (PA4) edges to reset SPI state, and /OC (PA0) faults.
 * @return  None
 */
void EXTI7_0_IRQHandler(void) __attribute__((interrupt));
void EXTI7_0_IRQHandler(void)
{
	if (EXTI_GetITStatus(SPI1_NSS_EXTI_LINE) != RESET)
	{
		/* NSS (CS) の状態を確認 */
		if (GPIO_ReadInputDataBit(SPI1_NSS_GPIO_PORT, SPI1_NSS_GPIO_PIN) == Bit_RESET)
		{
			/* --- 通信開始 (FALLING) : 負荷を極小にする --- */
			spi_rx_cnt = 0;
			spi_curr_cmd = 0;
		}
		else
		{
			/* --- 通信終了 (RISING) : アイドル中に次回の準備を済ませる --- */

			/* CS立ち上がり時にアクティブなDMA転送を即座に停止 */
			DMA1_Channel3->CFGR = s_dma_cfgr_base;
			SPI1->CTLR2 = s_spi_ctlr2_dma_off;

			/* ハードウェア・ビットカウンタの物理的クリア (SPEトグル)
			 * 余裕のあるアイドル中に行うことで、次回の通信は「準備完了」からスタート
			 */
			SPI1->CTLR1 &= ~SPI_CTLR1_SPE;
			SPI1->CTLR1 |= SPI_CTLR1_SPE;

			/* FIFOフラッシュ */
			while (SPI1->STATR & SPI_STATR_RXNE)
				(void)SPI1->DATAR;
			(void)SPI1->STATR;
		}

		EXTI_ClearITPendingBit(SPI1_NSS_EXTI_LINE);
	}

	/* /OC のチェック */
	if (EXTI_GetITStatus(EXTI_Line0) != RESET)
	{
		/* 外部ロードスイッチからの /OC 信号を検知 */
		if (GPIO_ReadInputDataBit(OC_GPIO_PORT, OC_GPIO_PIN) == OC_ON)
		{
			USBC_Source_HandleOC();
		}
		EXTI_ClearITPendingBit(EXTI_Line0);
	}
}

/**
 * @brief   SPI1 Interrupt Handler.
 *          Handles command parsing on RX and initiates DMA for TX responses.
 * @return  None
 */
void SPI1_IRQHandler(void) __attribute__((interrupt));
void SPI1_IRQHandler(void)
{
	if (SPI1->STATR & SPI_STATR_RXNE)
	{
		uint8_t rx_data = SPI1->DATAR;

		if (spi_rx_cnt == 0)
		{
			/* 1バイト目 = コマンドID */
			spi_curr_cmd = rx_data;
			spi_rx_cnt = 1;

			/* 応答ポインタと長さを事前に解決 (ダブルバッファリング対応) */
			uint8_t stable_idx = Gamepad_Stable_Idx;

			switch (spi_curr_cmd)
			{
				case CMD_GET_PAD_INPUT:	 // 0x01 (解析済み 3バイト)
					spi_tx_ptr = Gamepad_SPI_Data[stable_idx];
					spi_tx_len = 3;
					break;

				case CMD_GET_PAD_RAW:  // 0x02 (生レポート)
					spi_tx_ptr = Gamepad_Raw_Report[stable_idx];
					spi_tx_len = Gamepad_Raw_Report_Len[stable_idx];
					break;

				case CMD_GET_PAD_RAW_LEN:  // 0x03 (生レポート長)
					spi_tx_ptr = &Gamepad_Raw_Report_Len[stable_idx];
					spi_tx_len = 1;
					break;

				case CMD_GET_SYS_STATUS:  // 0x10
					spi_res_status = (Gamepad_Status >= GAMEPAD_ENUMERATED) ? 1 : 0;
					spi_tx_ptr = &spi_res_status;
					spi_tx_len = 1;
					break;

				case CMD_GET_DEVICE_ID:	 // 0x11
					spi_res_id[0] = (uint8_t)(Gamepad_VID & 0xFF);
					spi_res_id[1] = (uint8_t)(Gamepad_VID >> 8);
					spi_res_id[2] = (uint8_t)(Gamepad_PID & 0xFF);
					spi_res_id[3] = (uint8_t)(Gamepad_PID >> 8);
					spi_tx_ptr = spi_res_id;
					spi_tx_len = 4;
					break;

				case CMD_GET_FW_VERSION:  // 0xFE
					spi_tx_ptr = spi_res_version;
					spi_tx_len = 2;
					break;

				default:
					spi_tx_ptr = &spi_res_null;
					spi_tx_len = 0;
					break;
			}

			if (spi_tx_len > 0)
			{
				/* RMWなしの直接書き込みでDMAセットアップ時間を短縮 */
				DMA1_Channel3->CFGR  = s_dma_cfgr_base;
				DMA1_Channel3->MADDR = (uint32_t)spi_tx_ptr;
				DMA1_Channel3->CNTR  = spi_tx_len;
				DMA1_Channel3->CFGR  = s_dma_cfgr_enable;
				SPI1->CTLR2          = s_spi_ctlr2_dma_on;
			}
			else
			{
				SPI1->DATAR = 0x00;
			}
		}
		else
		{
			/* 2バイト目以降: TXはDMAが処理しています。
			 * 内部状態/デバッグのためにカウンターのみ加算します。 */
			spi_rx_cnt++;
		}
	}
}


