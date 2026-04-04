#include "usb_host_gamepad.h"
#include "usb_host_config.h"
#include "spi_slave.h"
#include "ch32x035_pwr.h"
#include "ch32x035_usbfs_host.h"
#include "ch32x035_tim.h"
#include "debug.h"
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

/* Debounce & SPI Buffer */
uint8_t Gamepad_SPI_Final[3] = {0};
uint32_t Button_Release_Timer[32] = {0}; // Blanking timer for each bit
uint32_t Current_System_Time = 0;        // Incremented in TIM3

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
    /* Example Mapping for Standard HID (Common for generic controllers)
     * Byte 0-1: Analog sticks
     * Byte 2-3: D-pad/Buttons
     * This may need tuning per device.
     */
    if (len < 6) return;

    // Example: Buttons in Byte 3, 4, 5
    // Byte 0: A B X Y L1 L2 R1 R2 (Bits 0-7)
    Gamepad_Debounce_Update(0, report[3] & 0x01); // A
    Gamepad_Debounce_Update(1, report[3] & 0x02); // B
    Gamepad_Debounce_Update(2, report[3] & 0x04); // X
    Gamepad_Debounce_Update(3, report[3] & 0x08); // Y
    Gamepad_Debounce_Update(4, report[3] & 0x10); // L1
    Gamepad_Debounce_Update(5, report[3] & 0x20); // L2
    Gamepad_Debounce_Update(6, report[3] & 0x40); // R1
    Gamepad_Debounce_Update(7, report[3] & 0x80); // R2

    // Byte 1: Start Select L3 R3 Up Down Left Right (Bits 8-15)
    Gamepad_Debounce_Update(8, report[4] & 0x01); // Start
    Gamepad_Debounce_Update(9, report[4] & 0x02); // Select
    Gamepad_Debounce_Update(10, report[4] & 0x04); // L3
    Gamepad_Debounce_Update(11, report[4] & 0x08); // R3
    Gamepad_Debounce_Update(12, report[4] & 0x10); // Up
    Gamepad_Debounce_Update(13, report[4] & 0x20); // Down
    Gamepad_Debounce_Update(14, report[4] & 0x40); // Left
    Gamepad_Debounce_Update(15, report[4] & 0x80); // Right

    // Byte 2: Digitalized Analogs (if any, report[0-2])
    // Simplified: Just copy some raw analog triggers as digital for now
    Gamepad_Debounce_Update(16, report[5] & 0x01); // Analog 1
    // ...

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
                    if (s == ERR_SUCCESS && len > 0)
                    {
                        Gamepad_Data_Map(Gamepad_Report_Buf, len);

                        /* Ignore the last 8 bytes (custom extensions like IMU/Accel) for change detection */
                        uint16_t relevant_len = (len > 8) ? (len - 8) : len;

                        if (memcmp(Gamepad_Report_Buf, Gamepad_Prev_Report_Buf, relevant_len) != 0)
                        {
                            printf("HID Data: ");
                            for(int i=0; i<relevant_len; i++) printf("%02x ", Gamepad_Report_Buf[i]);
                            printf("\r\n");
                            
                            /* Update previous report buffer for comparison next time */
                            memcpy(Gamepad_Prev_Report_Buf, Gamepad_Report_Buf, len);
                            
                            // Visual Feedback: Toggle LED when actual buttons/sticks move
                            GPIO_WriteBit(GPIOA, GPIO_Pin_0, !GPIO_ReadOutputDataBit(GPIOA, GPIO_Pin_0));
                        }
                    }
                }
            }
        }
    }
}
