#include "debug.h"

int main(void)
{
    /* 1. STARTUP SIGNAL: Light LED at the very beginning */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    GPIO_InitTypeDef GPIO_LED = {0};
    GPIO_LED.GPIO_Pin = GPIO_Pin_0;
    GPIO_LED.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_LED.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_LED);
    GPIO_WriteBit(GPIOA, GPIO_Pin_0, Bit_SET); /* LED ON during stabilization */

    /* 2. CORE INIT */
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);
    SystemCoreClockUpdate();
    Delay_Init();

    /* 3. UART INIT: PB10 (Pin 21, Default) 115200bps */
    USART_Printf_Init(115200);

    printf("\r\n\r\n=== SDK-STANDARD UART READY ===\r\n");
    printf("Baudrate: 115200 / Pin: PB10 (Mode 1 on Pin 21)\r\n");

    while(1)
    {
        printf("Heartbeat: Active (Standard SDK Init)\r\n");

        /* HEARTBEAT LOOP (1 second) */
        for(int j = 0; j < 20; j++)
        {
            GPIO_WriteBit(GPIOA, GPIO_Pin_0, !GPIO_ReadOutputDataBit(GPIOA, GPIO_Pin_0));
            Delay_Ms(50);
        }
    }
}
