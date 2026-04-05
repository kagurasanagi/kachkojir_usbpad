#include "spi_slave.h"
#include "usb_host_gamepad.h"
#include <string.h>

/* Global communication state */
volatile uint8_t spi_rx_cnt = 0;
volatile uint8_t spi_curr_cmd = 0;
uint8_t spi_tx_tmp[SPI_BUFF_SIZE]; // Temporary response buffer

/* Optimization: DMA-based response for 1MHz jitter-free reliability */
static uint8_t  spi_tx_snapshot[64]; // Snapshot buffer to prevent race conditions
static uint8_t  spi_res_null = 0;
static uint8_t *spi_tx_ptr = &spi_res_null;
static uint8_t  spi_tx_len = 0;

/* Static response buffers for constant/constructed values */
static uint8_t spi_res_status;
static uint8_t spi_res_version[2] = {0x01, 0x09}; // v1.9
static uint8_t spi_res_id[4];

/*********************************************************************
 * @fn      SPI1_Slave_Init
 * @brief   Initialize SPI1 as Slave with EXTI for CS syncing.
 */
void SPI1_Slave_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    SPI_InitTypeDef  SPI_InitStructure = {0};
    NVIC_InitTypeDef NVIC_InitStructure = {0};
    EXTI_InitTypeDef EXTI_InitStructure = {0};

    /* Enable Clocks: PA, SPI1, AFIO */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_SPI1 | RCC_APB2Periph_AFIO, ENABLE);

    /* SPI1 GPIOs: PA4:NSS, PA5:SCK, PA6:MISO, PA7:MOSI */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_5 | GPIO_Pin_7;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* PA4 as EXTI for CS Sync (Falling edge) */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_4;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU; // Internal Pull-up
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    GPIO_EXTILineConfig(GPIO_PortSourceGPIOA, GPIO_PinSource4);
    EXTI_InitStructure.EXTI_Line = EXTI_Line4;
    EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Falling;
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;
    EXTI_Init(&EXTI_InitStructure);

    /* SPI1 Config */
    SPI_InitStructure.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
    SPI_InitStructure.SPI_Mode = SPI_Mode_Slave;
    SPI_InitStructure.SPI_DataSize = SPI_DataSize_8b;
    SPI_InitStructure.SPI_CPOL = SPI_CPOL_Low;   // Mode 0 as per readme
    SPI_InitStructure.SPI_CPHA = SPI_CPHA_1Edge; // Mode 0 as per readme
    SPI_InitStructure.SPI_NSS = SPI_NSS_Soft;    // Use EXTI for custom state handling
    SPI_InitStructure.SPI_FirstBit = SPI_FirstBit_MSB;
    SPI_InitStructure.SPI_CRCPolynomial = 7;
    SPI_Init(SPI1, &SPI_InitStructure);

    /* Enable RX Interrupt for Command parsing */
    SPI_I2S_ITConfig(SPI1, SPI_I2S_IT_RXNE, ENABLE);

    /* NVIC Config for SPI and EXTI */
    NVIC_InitStructure.NVIC_IRQChannel = SPI1_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0; // Highest
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;        // Highest
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    NVIC_InitStructure.NVIC_IRQChannel = EXTI7_0_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;        // High for CS Sync
    NVIC_Init(&NVIC_InitStructure);

    SPI_Cmd(SPI1, ENABLE);

    /* Initialize DMA for stable high-speed transmission */
    SPI1_DMA_Init();
}

/*********************************************************************
 * @fn      SPI1_DMA_Init
 * @brief   Initialize DMA1 for multi-byte responses if needed.
 *          (Currently we use Interrupt for logic, DMA can be added later for RAW data)
 */
void SPI1_DMA_Init(void)
{
    DMA_InitTypeDef DMA_InitStructure = {0};

    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);

    /* DMA1 Channel 3 for SPI1 TX */
    DMA_DeInit(DMA1_Channel3);
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&SPI1->DATAR;
    DMA_InitStructure.DMA_MemoryBaseAddr = (uint32_t)spi_tx_snapshot;
    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralDST;
    DMA_InitStructure.DMA_BufferSize = 0; // Set upon command
    DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
    DMA_InitStructure.DMA_Mode = DMA_Mode_Normal;
    DMA_InitStructure.DMA_Priority = DMA_Priority_VeryHigh;
    DMA_InitStructure.DMA_M2M = DMA_M2M_Disable;
    DMA_Init(DMA1_Channel3, &DMA_InitStructure);
}

/*********************************************************************
 * @fn      EXTI7_0_IRQHandler
 * @brief   NSS (PA4) Falling Edge - Reset communication state.
 */
void EXTI7_0_IRQHandler(void) __attribute__((interrupt));
void EXTI7_0_IRQHandler(void)
{
    if (EXTI_GetITStatus(EXTI_Line4) != RESET)
    {
        spi_rx_cnt = 0;
        spi_curr_cmd = 0;

        /* Stop any active DMA transfer on CS reset */
        DMA1_Channel3->CFGR &= ~DMA_CFGR1_EN;
        SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Tx, DISABLE);

        /* Clear any pending SPI flags */
        (void)SPI1->DATAR;
        (void)SPI1->STATR;
        EXTI_ClearITPendingBit(EXTI_Line4);
    }
}

/*********************************************************************
 * @fn      SPI1_IRQHandler
 * @brief   Handle SPI interrupts for Command Parsing and Response.
 */
void SPI1_IRQHandler(void) __attribute__((interrupt));
void SPI1_IRQHandler(void)
{
    if (SPI1->STATR & SPI_STATR_RXNE)
    {
        uint8_t rx_data = SPI1->DATAR;

        if (spi_rx_cnt == 0)
        {
            /* 1st Byte = Command ID */
            spi_curr_cmd = rx_data;
            spi_rx_cnt = 1;

            /* Pre-resolve response pointer and length */
            switch (spi_curr_cmd)
            {
                case CMD_GET_PAD_INPUT:   // 0x01
                    spi_tx_ptr = Gamepad_SPI_Final;
                    spi_tx_len = 3;
                    break;

                case CMD_GET_PAD_RAW:     // 0x02
                    spi_tx_ptr = Gamepad_Report_Buf;
                    spi_tx_len = 64;
                    break;

                case CMD_GET_PAD_RAW_LEN: // 0x03
                    spi_tx_ptr = &Gamepad_Raw_Len;
                    spi_tx_len = 1;
                    break;

                case CMD_GET_SYS_STATUS:  // 0x10
                    spi_res_status = (Gamepad_Status >= GAMEPAD_ENUMERATED) ? 1 : 0;
                    spi_tx_ptr = &spi_res_status;
                    spi_tx_len = 1;
                    break;

                case CMD_GET_DEVICE_ID:   // 0x11
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
                /* Snapshot data to prevent race conditions during transmission */
                memcpy(spi_tx_snapshot, spi_tx_ptr, spi_tx_len);

                /* Start DMA for jitter-free response */
                DMA1_Channel3->CFGR &= ~DMA_CFGR1_EN; // Ensure disabled
                DMA1_Channel3->MADDR = (uint32_t)spi_tx_snapshot;
                DMA1_Channel3->CNTR = spi_tx_len;
                DMA1_Channel3->CFGR |= DMA_CFGR1_EN;  // Enable

                SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Tx, ENABLE);
            }
            else
            {
                SPI1->DATAR = 0x00;
            }
        }
        else
        {
            /* 2nd byte and onwards: DMA is handling the TX.
             * We increment the counter just for internal state/debug. */
            spi_rx_cnt++;
        }
    }
}

void SPI1_Update_Data(uint8_t *data)
{
    /* Handled by volatile Gamepad_SPI_Final in ISR */
}
