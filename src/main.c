#include "debug.h"
#include "usb_host_gamepad.h"

int main(void)
{
    /* 1. LED GPIO Init (PA0, Pin 5) - OFF by default */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    GPIO_InitTypeDef GPIO_LED = {0};
    GPIO_LED.GPIO_Pin = GPIO_Pin_0;
    GPIO_LED.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_LED.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_LED);
    GPIO_WriteBit(GPIOA, GPIO_Pin_0, Bit_RESET); /* LED OFF */

    /* 2. Core Init */
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);
    SystemCoreClockUpdate();
    Delay_Init();

    /* 3. UART Init: PB10 (Pin 21, Default) 115200bps */
    USART_Printf_Init(115200);
    printf("\r\n=== Kachkojir USB Pad ===\r\n");

    /* 4. USB Host Init */
    USB_Host_Init_Sequence();

    /* 5. Main Loop: Poll USB Host */
    while(1)
    {
        USBH_Process();
    }
}
