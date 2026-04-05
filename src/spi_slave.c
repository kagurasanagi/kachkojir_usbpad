#include "spi_slave.h"
#include "usb_host_gamepad.h"
#include <string.h>

/* Global communication state */
volatile uint8_t spi_rx_cnt = 0;
volatile uint8_t spi_curr_cmd = 0;
uint8_t spi_tx_tmp[SPI_BUFF_SIZE]; // Temporary response buffer

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
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    NVIC_InitStructure.NVIC_IRQChannel = EXTI7_0_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0; // Highest priority for sync
    NVIC_Init(&NVIC_InitStructure);

    SPI_Cmd(SPI1, ENABLE);
}

/*********************************************************************
 * @fn      SPI1_DMA_Init
 * @brief   Initialize DMA1 for multi-byte responses if needed.
 *          (Currently we use Interrupt for logic, DMA can be added later for RAW data)
 */
void SPI1_DMA_Init(void)
{
    // Simplified: For now we handle v1.9 commands in ISR for flexibility.
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
    if (SPI_I2S_GetITStatus(SPI1, SPI_I2S_IT_RXNE) != RESET)
    {
        uint8_t rx_data = SPI1->DATAR;

        if (spi_rx_cnt == 0)
        {
            /* 1st Byte = Command ID */
            spi_curr_cmd = rx_data;
            spi_rx_cnt = 1;

            /* Prepare first response byte based on command */
            switch (spi_curr_cmd)
            {
                case CMD_GET_PAD_INPUT:   // 0x01
                    /* Response: 3 bytes mapping. 1st byte of response is SPI_Final[0] */
                    SPI1->DATAR = Gamepad_SPI_Final[0];
                    break;

                case CMD_GET_PAD_RAW_LEN: // 0x03
                    SPI1->DATAR = Gamepad_Raw_Len;
                    break;

                case CMD_GET_PAD_RAW:     // 0x02
                    SPI1->DATAR = Gamepad_Report_Buf[0];
                    break;

                case CMD_GET_SYS_STATUS:  // 0x10
                    SPI1->DATAR = (Gamepad_Status >= GAMEPAD_ENUMERATED) ? 1 : 0;
                    break;

                case CMD_GET_DEVICE_ID:   // 0x11
                    /* VID(2B) + PID(2B). 1st byte is VID Low */
                    SPI1->DATAR = (uint8_t)(Gamepad_VID & 0xFF);
                    break;

                case CMD_GET_FW_VERSION:  // 0xFE
                    SPI1->DATAR = 0x01; // Major 1
                    break;

                default:
                    SPI1->DATAR = 0x00;
                    break;
            }
        }
        else
        {
            /* 2nd byte and onwards: Send sequence data */
            switch (spi_curr_cmd)
            {
                case CMD_GET_PAD_INPUT:
                    if (spi_rx_cnt == 1)      SPI1->DATAR = Gamepad_SPI_Final[1];
                    else if (spi_rx_cnt == 2) SPI1->DATAR = Gamepad_SPI_Final[2];
                    else                     SPI1->DATAR = 0x00;
                    break;

                case CMD_GET_PAD_RAW:
                    if (spi_rx_cnt < 64)     SPI1->DATAR = Gamepad_Report_Buf[spi_rx_cnt];
                    else                     SPI1->DATAR = 0x00;
                    break;

                case CMD_GET_DEVICE_ID:
                    if (spi_rx_cnt == 1)      SPI1->DATAR = (uint8_t)(Gamepad_VID >> 8);
                    else if (spi_rx_cnt == 2) SPI1->DATAR = (uint8_t)(Gamepad_PID & 0xFF);
                    else if (spi_rx_cnt == 3) SPI1->DATAR = (uint8_t)(Gamepad_PID >> 8);
                    else                     SPI1->DATAR = 0x00;
                    break;
                
                case CMD_GET_FW_VERSION:
                    if (spi_rx_cnt == 1)      SPI1->DATAR = 0x09; // Minor 9 (v1.9)
                    else                     SPI1->DATAR = 0x00;
                    break;

                default:
                    SPI1->DATAR = 0x00;
                    break;
            }
            spi_rx_cnt++;
        }
    }
}

void SPI1_Update_Data(uint8_t *data)
{
    /* Handled by volatile Gamepad_SPI_Final in ISR */
}
