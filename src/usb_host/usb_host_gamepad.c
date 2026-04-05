#include "usb_host_gamepad.h"
#include "usb_host_config.h"
#include "spi_slave.h"
#include "ch32x035_pwr.h"
#include "ch32x035_usbfs_host.h"
#include "ch32x035_tim.h"
#include "debug.h"
#include "ch32x035_gpio.h"
#include <string.h>

/*******************************************************************************/
/* Variable Definition */
uint8_t DevDesc_Buf[18];
uint8_t Com_Buf[DEF_COM_BUF_LEN];
struct _ROOT_HUB_DEVICE RootHubDev;
struct __HOST_CTL HostCtl[DEF_TOTAL_ROOT_HUB * DEF_ONE_USB_SUP_DEV_TOTAL];

uint8_t Gamepad_Status = GAMEPAD_DISCONNECT;
uint8_t Gamepad_Report_Buf[64];
uint8_t Gamepad_Prev_Report_Buf[64];
uint8_t Gamepad_SPI_Prev[3] = {0}; // For 24-bit binary change detection
static uint8_t Gamepad_Comm_Ready = 0; // 1 = Communication responding after HOME button
static uint32_t PA1_Last_Success_Time = 0; // Last system time with valid PID response
static uint32_t Gamepad_Data_Last_Time = 0; // Last time a valid data packet (ERR_SUCCESS) arrived

/* Debounce & SPI Buffer */
uint8_t Gamepad_SPI_Final[3] = {0};
uint32_t Button_Release_Timer[32] = {0}; // Blanking timer for each bit
uint32_t Current_System_Time = 0;        // Incremented in TIM3

uint16_t Gamepad_VID = 0;
uint16_t Gamepad_PID = 0;
uint8_t  Gamepad_Raw_Len = 0;

#define DEBOUNCE_BLANKING_MS 8

/*******************************************************************************
 * @fn      USB_Host_Init_Sequence
 * @brief   Initialize USBFS host and peripherals.
 */
void USB_Host_Init_Sequence(void)
{
    /* Initialize USBFS host */
    printf("USBFS Host Init (Supply=%dV)\r\n", (PWR_VDD_SupplyVoltage() == PWR_VDD_5V) ? 5 : 3);
    USBFS_RCC_Init();
    USBFS_Host_Init(ENABLE, PWR_VDD_SupplyVoltage());
    memset(&RootHubDev, 0, sizeof(RootHubDev));
    memset(HostCtl, 0, sizeof(HOST_CTL) * DEF_TOTAL_ROOT_HUB * DEF_ONE_USB_SUP_DEV_TOTAL);

    /* LED Init: PA1 (PadEnable), PA2 (Status) 
     * GPIOPA_CFGLR (0x40010800): PA1 Bits 7-4, PA2 Bits 11-8
     * MODE: 11 (Output 50MHz), CNF: 00 (Push-Pull) -> 0x3
     */
    {
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
        GPIOA->CFGLR &= ~( (0x0F << 4) | (0x0F << 8) ); // Clear PA1, PA2 bits
        GPIOA->CFGLR |= ( (0x03 << 4) | (0x03 << 8) );  // Set PA1, PA2 to 0x3 (PP 50M)
        GPIO_WriteBit(GPIOA, GPIO_Pin_1, Bit_RESET);
        GPIO_WriteBit(GPIOA, GPIO_Pin_2, Bit_RESET);
    }

    /* Initialize SPI Slave for RP2350 communication */
    SPI1_Slave_Init();
    SPI1_DMA_Init();

    /* Initialize Timer for 1ms ticks */
    TIM3_Init(9, SystemCoreClock / 10000 - 1);

    if (SystemCoreClock != 48000000)
    {
        printf("WARNING: SystemCoreClock is NOT 48MHz! USBFS may fail.\r\n");
    }
    printf("Waiting for Gamepad...\r\n");
}

/*******************************************************************************
 * @fn      TIM3_Init
 * @brief   Initialize timer3 for polling interval counting.
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
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 2;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    TIM_Cmd(TIM3, ENABLE);
}

/*******************************************************************************
 * @fn      TIM3_IRQHandler
 * @brief   Forwarding delay logic for USB endpoints.
 */
void TIM3_IRQHandler(void) __attribute__((interrupt));
void TIM3_IRQHandler(void)
{
    uint8_t intf, in_num;
    if (TIM_GetITStatus(TIM3, TIM_IT_Update) != RESET)
    {
        TIM_ClearITPendingBit(TIM3, TIM_IT_Update);
        Current_System_Time++; // 1ms tick
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
    if (enum_cnt > 3) return ERR_USB_UNSUPPORT;

    USBFSH_ResetRootHubPort(0);
    for (i = 0, s = 0; i < DEF_RE_ATTACH_TIMEOUT; i++)
    {
        if (USBFSH_EnableRootHubPort(&RootHubDev.bSpeed) == ERR_SUCCESS)
        {
            s++; if (s > 10) break;
        }
        Delay_Ms(1);
    }
    if (i >= DEF_RE_ATTACH_TIMEOUT) goto ENUM_START;

    s = USBFSH_GetDeviceDescr(&RootHubDev.bEp0MaxPks, DevDesc_Buf);
    if (s != ERR_SUCCESS) goto ENUM_START;

    /* Extract VID and PID */
    Gamepad_VID = DevDesc_Buf[8] | ((uint16_t)DevDesc_Buf[9] << 8);
    Gamepad_PID = DevDesc_Buf[10] | ((uint16_t)DevDesc_Buf[11] << 8);

    RootHubDev.bAddress = (uint8_t)(USB_DEVICE_ADDR);
    s = USBFSH_SetUsbAddress(RootHubDev.bEp0MaxPks, RootHubDev.bAddress);
    if (s != ERR_SUCCESS) goto ENUM_START;
    Delay_Ms(5);

    s = USBFSH_GetConfigDescr(RootHubDev.bEp0MaxPks, Com_Buf, DEF_COM_BUF_LEN, &len);
    if (s == ERR_SUCCESS)
    {
        cfg_val = ((PUSB_CFG_DESCR)Com_Buf)->bConfigurationValue;
        USBH_AnalyseType(DevDesc_Buf, Com_Buf, &RootHubDev.bType);
    }
    else goto ENUM_START;

    s = USBFSH_SetUsbConfig(RootHubDev.bEp0MaxPks, cfg_val);
    if (s != ERR_SUCCESS) return ERR_USB_UNSUPPORT;

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
    for (i = 0; i < total_len; )
    {
        if (Com_Buf[i + 1] == DEF_DECR_CONFIG)
        {
            HostCtl[index].InterfaceNum = ((PUSB_CFG_DESCR)(&Com_Buf[i]))->bNumInterfaces;
            i += Com_Buf[i];
        }
        else if (Com_Buf[i + 1] == DEF_DECR_INTERFACE)
        {
            if (((PUSB_ITF_DESCR)(&Com_Buf[i]))->bInterfaceClass == 0x03) // HID
            {
                HostCtl[index].Interface[num].Type = 0xFF; 
                i += Com_Buf[i];
                innum = 0;
                while (i < total_len && Com_Buf[i+1] != DEF_DECR_INTERFACE)
                {
                    if (Com_Buf[i + 1] == DEF_DECR_ENDPOINT)
                    {
                        if (((PUSB_ENDP_DESCR)(&Com_Buf[i]))->bEndpointAddress & 0x80)
                        {
                            HostCtl[index].Interface[num].InEndpAddr[innum] = ((PUSB_ENDP_DESCR)(&Com_Buf[i]))->bEndpointAddress & 0x7F;
                            HostCtl[index].Interface[num].InEndpSize[innum] = ((PUSB_ENDP_DESCR)(&Com_Buf[i]))->wMaxPacketSizeL;
                            HostCtl[index].Interface[num].InEndpInterval[innum] = ((PUSB_ENDP_DESCR)(&Com_Buf[i]))->bInterval;
                            HostCtl[index].Interface[num].InEndpNum++;
                            innum++;
                        }
                    }
                    i += Com_Buf[i];
                }
            }
            else i += Com_Buf[i];
            num++;
        }
        else i += Com_Buf[i];
    }
    return ERR_SUCCESS;
}

/*******************************************************************************
 * @fn      Gamepad_Debounce_Update
 * @brief   Response-Priority Debounce: Instant Press, Blanking Release.
 */
void Gamepad_Debounce_Update(uint8_t bit_idx, uint8_t raw_state)
{
    uint8_t byte_idx = bit_idx / 8;
    uint8_t bit_mask = (1 << (bit_idx % 8));

    if (raw_state)
    {
        /* Instant Press */
        Gamepad_SPI_Final[byte_idx] |= bit_mask;
        Button_Release_Timer[bit_idx] = 0; // Reset timer on press
    }
    else
    {
        /* Release with Blanking */
        if (Gamepad_SPI_Final[byte_idx] & bit_mask)
        {
            if (Button_Release_Timer[bit_idx] == 0)
            {
                Button_Release_Timer[bit_idx] = Current_System_Time;
            }
            else if ((Current_System_Time - Button_Release_Timer[bit_idx]) >= DEBOUNCE_BLANKING_MS)
            {
                Gamepad_SPI_Final[byte_idx] &= ~bit_mask;
                Button_Release_Timer[bit_idx] = 0;
            }
        }
    }
}

/*******************************************************************************
 * @fn      Gamepad_Data_Map
 * @brief   Map HID report bits to SPI buffer and apply debouncing.
 *          Note: This assumes a common X-Input / Standard HID layout.
 */
void Gamepad_Data_Map(uint8_t *report, uint16_t len)
{
    /* Based on user-provided HID Data:
     * Buttons (A,B,X,Y): report[0] (0x01, 0x02, 0x04, 0x08)
     * D-pad (Hat Switch): report[2] (0-7, 15=neutral)
     * Sticks: LX=report[3], LY=report[4], RX=report[5], RY=report[6]
     */
    if (len < 7) return;

    /* --- Byte 0: Buttons (bit_idx 7-0) --- v1.9 Mapping: A B X Y L1 R1 L2 R2 */
    Gamepad_Debounce_Update(7, report[0] & 0x01); // A (bit0)
    Gamepad_Debounce_Update(6, report[0] & 0x02); // B (bit1)
    Gamepad_Debounce_Update(5, report[0] & 0x04); // X (bit2)
    Gamepad_Debounce_Update(4, report[0] & 0x08); // Y (bit3)
    Gamepad_Debounce_Update(3, report[0] & 0x10); // L1 (bit4)
    Gamepad_Debounce_Update(2, report[0] & 0x20); // R1 (bit5)
    Gamepad_Debounce_Update(1, report[0] & 0x40); // L2 (bit6)
    Gamepad_Debounce_Update(0, report[0] & 0x80); // R2 (bit7)

    /* --- Byte 1: Controls (bit_idx 15-8) --- v1.9 Mapping: Start Select L3 R3 Up Right Down Left */
    Gamepad_Debounce_Update(15, report[1] & 0x02); // Start (bit1)
    Gamepad_Debounce_Update(14, report[1] & 0x01); // Select (bit0)
    Gamepad_Debounce_Update(13, report[1] & 0x04); // L3 (bit2)
    Gamepad_Debounce_Update(12, report[1] & 0x08); // R3 (bit3)

    // D-pad: Bit order changed to Up | Right | Down | Left
    uint8_t dpad = report[2] & 0x0F;
    Gamepad_Debounce_Update(11, (dpad == 0x00)); // Up
    Gamepad_Debounce_Update(10, (dpad == 0x02)); // Right
    Gamepad_Debounce_Update(9,  (dpad == 0x04)); // Down
    Gamepad_Debounce_Update(8,  (dpad == 0x06)); // Left

    /* --- Byte 2: Analogs Thresholding (bit_idx 23-16) --- Order: L-U | L-R | L-D | L-L | R-U | R-R | R-D | R-L */
    // Left Stick (X=report[3], Y=report[4])
    Gamepad_Debounce_Update(23, report[4] < 0x40); // L-Up
    Gamepad_Debounce_Update(22, report[3] > 0xC0); // L-Right
    Gamepad_Debounce_Update(21, report[4] > 0xC0); // L-Down
    Gamepad_Debounce_Update(20, report[3] < 0x40); // L-Left

    // Right Stick (X=report[5], Y=report[6])
    Gamepad_Debounce_Update(19, report[6] < 0x40); // R-Up
    Gamepad_Debounce_Update(18, report[5] > 0xC0); // R-Right
    Gamepad_Debounce_Update(17, report[6] > 0xC0); // R-Down
    Gamepad_Debounce_Update(16, report[5] < 0x40); // R-Left

    /* Synchronize with SPI Slave */
    SPI1_Update_Data(Gamepad_SPI_Final);
}

/*******************************************************************************
 * @fn      USBH_Process
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
            printf("Device Attached\r\n");
            uint8_t enum_res = USBH_EnumRootDevice();
            if (enum_res == ERR_SUCCESS)
            {
                if (RootHubDev.bType == USB_DEV_CLASS_HID)
                {
                    GAMEPAD_AnalyzeConfigDesc(0, RootHubDev.bEp0MaxPks);
                    Gamepad_Status = GAMEPAD_ENUMERATED;
                    printf("Gamepad OK (EP0=%d, Intf=%d)\r\n",
                           RootHubDev.bEp0MaxPks, HostCtl[0].InterfaceNum);
                }
                else
                {
                    printf("Not HID: type=%d\r\n", RootHubDev.bType);
                }
            }
            else
            {
                printf("Enum Fail: %02x\r\n", enum_res);
            }
        }
        else if (s == ROOT_DEV_DISCONNECT)
        {
            printf("Device Detached\r\n");
            Gamepad_Status = GAMEPAD_DISCONNECT;
            memset(&HostCtl, 0, sizeof(HostCtl));
            memset(Gamepad_SPI_Final, 0, 3);
            memset(Button_Release_Timer, 0, sizeof(Button_Release_Timer));
            SPI1_Update_Data(Gamepad_SPI_Final);
        }
        else if (s == ROOT_DEV_FAILED)
        {
            /* Reset to DISCONNECT so the state machine can re-detect */
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
                if (HostCtl[0].Interface[intf].InEndpTimeCount[in_num] >= HostCtl[0].Interface[intf].InEndpInterval[in_num])
                {
                    HostCtl[0].Interface[intf].InEndpTimeCount[in_num] = 0;
                    len = HostCtl[0].Interface[intf].InEndpSize[in_num];
                    s = USBFSH_GetEndpData(HostCtl[0].Interface[intf].InEndpAddr[in_num], \
                                           &HostCtl[0].Interface[intf].InEndpTog[in_num], Gamepad_Report_Buf, &len);
                    
                    /* --- Register-Based PA1 Update with 100ms Stability Delay --- */
                    if (USBFSH->MIS_ST & USBFS_UMS_DEV_ATTACH)
                    {
                        uint8_t res = USBFSH->INT_ST & 0x0F;
                        if (res != 0x00)
                        {
                            PA1_Last_Success_Time = Current_System_Time; /* Response OK: Record success time */
                        }
                    }

                    if (s == ERR_SUCCESS && len > 0)
                    {
                        Gamepad_Data_Last_Time = Current_System_Time;
                        Gamepad_Raw_Len = (uint8_t)len;
                        Gamepad_Data_Map(Gamepad_Report_Buf, len);

                        /* Optional: Log on report change only (minimal logging) */
                        if (memcmp(Gamepad_Report_Buf, Gamepad_Prev_Report_Buf, len) != 0)
                        {
                            memcpy(Gamepad_Prev_Report_Buf, Gamepad_Report_Buf, len);
                        }
                    }
                }
            }
        }
    }

    /* --- Constant Physical Disconnect Check --- */
    if (!(USBFSH->MIS_ST & USBFS_UMS_DEV_ATTACH))
    {
        /* Physically disconnected (R8_MIS_ST bit 0 == 0) */
        PA1_Last_Success_Time = Current_System_Time - 1000;
        Gamepad_Comm_Ready = 0;
        memset(Gamepad_SPI_Final, 0, sizeof(Gamepad_SPI_Final));
        memset(Button_Release_Timer, 0, sizeof(Button_Release_Timer));
        GPIO_WriteBit(GPIOA, GPIO_Pin_1, Bit_RESET);
        GPIO_WriteBit(GPIOA, GPIO_Pin_2, Bit_RESET);
        Gamepad_Status = GAMEPAD_DISCONNECT;
        Gamepad_Data_Last_Time = 0;
    }

    /* Update Gamepad_Comm_Ready based on 100ms timeout of valid responses */
    if (Gamepad_Status == GAMEPAD_ENUMERATED) {
        Gamepad_Comm_Ready = ( (Current_System_Time - PA1_Last_Success_Time) < 100 );
    } else {
        Gamepad_Comm_Ready = 0;
    }

    /* PA1 Ready LED: Based on communication ready status */
    GPIO_WriteBit(GPIOA, GPIO_Pin_1, (Gamepad_Comm_Ready) ? Bit_SET : Bit_RESET);

    /* PA2 Status LED: Update constantly if data is fresh (within 100ms) AND any bit is set */
    uint8_t data_is_fresh = ( (Current_System_Time - Gamepad_Data_Last_Time) < 100 );
    if (data_is_fresh && (Gamepad_SPI_Final[0] || Gamepad_SPI_Final[1] || Gamepad_SPI_Final[2])) {
        GPIO_WriteBit(GPIOA, GPIO_Pin_2, Bit_SET);
    } else {
        GPIO_WriteBit(GPIOA, GPIO_Pin_2, Bit_RESET);
    }
}
