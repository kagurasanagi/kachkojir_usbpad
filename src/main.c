/********************************** (C) COPYRIGHT *******************************
 * File Name          : main.c
 * Author             : Antigravity
 * Version            : V1.0.0
 * Date               : 2026/04/04
 * Description        : Main program body (Step 1: Blinky)
 *******************************************************************************/

#include "debug.h"

/* Global define */

/* Global Variable */

/*********************************************************************
 * @fn      GPIO_Toggle_INIT
 *
 * @brief   Initializes GPIOA.0
 *
 * @return  none
 */
void GPIO_Toggle_INIT(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
}

/*********************************************************************
 * @fn      main
 *
 * @brief   Main program.
 *
 * @return  none
 */
int main(void)
{
    u8 i = 0;

    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);
    SystemCoreClockUpdate();
    Delay_Init();
    USART_Printf_Init(115200);
    printf("SystemClk:%d\r\n", (int)SystemCoreClock);
    printf("ChipID:%08x\r\n", (unsigned int)DBGMCU_GetCHIPID());
    printf("GPIO Toggle TEST (Step 1)\r\n");
    GPIO_Toggle_INIT();

    while(1)
    {
        Delay_Ms(500);
        GPIO_WriteBit(GPIOA, GPIO_Pin_0, (i == 0) ? (i = Bit_SET) : (i = Bit_RESET));
    }
}
