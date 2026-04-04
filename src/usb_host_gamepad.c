/********************************** (C) COPYRIGHT *******************************
 * File Name          : usb_host_gamepad.c
 * Author             : Antigravity (based on WCH examples)
 * Version            : V1.0.0
 * Date               : 2026/04/04
 * Description        : USB Host Gamepad handling implementation.
 *******************************************************************************/

#include "usb_host_config.h"

/*******************************************************************************/
/* Variable Definition */
uint8_t DevDesc_Buf[18];
uint8_t Com_Buf[DEF_COM_BUF_LEN];
struct _ROOT_HUB_DEVICE RootHubDev;
struct __HOST_CTL HostCtl[DEF_TOTAL_ROOT_HUB * DEF_ONE_USB_SUP_DEV_TOTAL];

uint8_t Gamepad_Status = GAMEPAD_DISCONNECT;
uint8_t Gamepad_Report_Buf[64];
uint8_t Gamepad_Prev_Report_Buf[64];

/*******************************************************************************
 * @fn      USBH_AnalyseType
 * @brief   Simply analyze USB device type.
 */
void USBH_AnalyseType(uint8_t *pdev_buf, uint8_t *pcfg_buf, uint8_t *ptype)
{
    uint8_t dv_cls, if_cls;
    dv_cls = ((PUSB_DEV_DESCR)pdev_buf)->bDeviceClass;
    if_cls = ((PUSB_CFG_DESCR_LONG)pcfg_buf)->itf_descr.bInterfaceClass;

    if ((dv_cls == USB_DEV_CLASS_HID) || (if_cls == USB_DEV_CLASS_HID))
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
 * @brief   Generally enumerate a device connected to host port.
 */
uint8_t USBH_EnumRootDevice(void)
{
    uint8_t s, enum_cnt, cfg_val;
    uint16_t i, len;

    DUG_PRINTF("Enum Start\r\n");
    enum_cnt = 0;

ENUM_START:
    Delay_Ms(100);
    enum_cnt++;
    Delay_Ms(8 << enum_cnt);

    USBFSH_ResetRootHubPort(0);
    for (i = 0, s = 0; i < DEF_RE_ATTACH_TIMEOUT; i++)
    {
        if (USBFSH_EnableRootHubPort(&RootHubDev.bSpeed) == ERR_SUCCESS)
        {
            i = 0; s++;
            if (s > 6) break;
        }
        Delay_Ms(1);
    }
    if (i) { if (enum_cnt <= 5) goto ENUM_START; return ERR_USB_DISCON; }

    /* Get Device Descriptor */
    s = USBFSH_GetDeviceDescr(&RootHubDev.bEp0MaxPks, DevDesc_Buf);
    if (s != ERR_SUCCESS) { if (enum_cnt <= 5) goto ENUM_START; return DEF_DEV_DESCR_GETFAIL; }

    /* Set Address */
    RootHubDev.bAddress = (uint8_t)(USB_DEVICE_ADDR);
    s = USBFSH_SetUsbAddress(RootHubDev.bEp0MaxPks, RootHubDev.bAddress);
    if (s != ERR_SUCCESS) { if (enum_cnt <= 5) goto ENUM_START; return DEF_DEV_ADDR_SETFAIL; }
    Delay_Ms(5);

    /* Get Config Descriptor */
    s = USBFSH_GetConfigDescr(RootHubDev.bEp0MaxPks, Com_Buf, DEF_COM_BUF_LEN, &len);
    if (s == ERR_SUCCESS)
    {
        cfg_val = ((PUSB_CFG_DESCR)Com_Buf)->bConfigurationValue;
        USBH_AnalyseType(DevDesc_Buf, Com_Buf, &RootHubDev.bType);
        DUG_PRINTF("DevType: %02x\r\n", RootHubDev.bType);
    }
    else { if (enum_cnt <= 5) goto ENUM_START; return DEF_CFG_DESCR_GETFAIL; }

    /* Set Configuration */
    s = USBFSH_SetUsbConfig(RootHubDev.bEp0MaxPks, cfg_val);
    if (s != ERR_SUCCESS) return ERR_USB_UNSUPPORT;

    return ERR_SUCCESS;
}

/*******************************************************************************
 * @fn      GAMEPAD_AnalyzeConfigDesc
 */
uint8_t GAMEPAD_AnalyzeConfigDesc(uint8_t index, uint8_t ep0_size)
{
    uint16_t i;
    uint8_t num = 0, innum = 0;

    for (i = 0; i < (Com_Buf[2] + ((uint16_t)Com_Buf[3] << 8)); )
    {
        if (Com_Buf[i + 1] == DEF_DECR_CONFIG)
        {
            HostCtl[index].InterfaceNum = ((PUSB_CFG_DESCR)(&Com_Buf[i]))->bNumInterfaces;
            i += Com_Buf[i];
        }
        else if (Com_Buf[i + 1] == DEF_DECR_INTERFACE)
        {
            if (num == DEF_INTERFACE_NUM_MAX) break;
            if (((PUSB_ITF_DESCR)(&Com_Buf[i]))->bInterfaceClass == 0x03) // HID
            {
                HostCtl[index].Interface[num].Type = 0xFF; // Generic HID / Gamepad
                i += Com_Buf[i];
                innum = 0;
                while (1)
                {
                    if ((Com_Buf[i + 1] == DEF_DECR_INTERFACE) || (i >= Com_Buf[2] + ((uint16_t)Com_Buf[3] << 8))) break;
                    if (Com_Buf[i + 1] == DEF_DECR_ENDPOINT)
                    {
                        if (((PUSB_ENDP_DESCR)(&Com_Buf[i]))->bEndpointAddress & 0x80)
                        {
                            HostCtl[index].Interface[num].InEndpAddr[innum] = ((PUSB_ENDP_DESCR)(&Com_Buf[i]))->bEndpointAddress & 0x0F;
                            HostCtl[index].Interface[num].InEndpSize[innum] = ((PUSB_ENDP_DESCR)(&Com_Buf[i]))->wMaxPacketSizeL;
                            HostCtl[index].Interface[num].InEndpInterval[innum] = ((PUSB_ENDP_DESCR)(&Com_Buf[i]))->bInterval;
                            HostCtl[index].Interface[num].InEndpNum++;
                            innum++;
                        }
                        i += Com_Buf[i];
                    }
                    else i += Com_Buf[i];
                }
            }
            else { i += Com_Buf[i]; }
            num++;
        }
        else i += Com_Buf[i];
    }
    return ERR_SUCCESS;
}

/*******************************************************************************
 * @fn      USBH_Process
 * @brief   Main state machine for USB Host.
 */
void USBH_Process(void)
{
    uint8_t s;
    static uint8_t last_status = ROOT_DEV_DISCONNECT;

    s = USBFSH_CheckRootHubPortEnable();
    if (s != last_status)
    {
        if (s == ROOT_DEV_CONNECTED)
        {
            DUG_PRINTF("Device Attached\r\n");
            if (USBH_EnumRootDevice() == ERR_SUCCESS)
            {
                if (RootHubDev.bType == USB_DEV_CLASS_HID)
                {
                    GAMEPAD_AnalyzeConfigDesc(0, RootHubDev.bEp0MaxPks);
                    Gamepad_Status = GAMEPAD_ENUMERATED;
                    DUG_PRINTF("Gamepad Enumerated\r\n");
                }
            }
        }
        else
        {
            DUG_PRINTF("Device Detached\r\n");
            Gamepad_Status = GAMEPAD_DISCONNECT;
            memset(&HostCtl, 0, sizeof(HostCtl));
        }
        last_status = s;
    }

    if (Gamepad_Status == GAMEPAD_ENUMERATED)
    {
        // Polling Gamepad data
        uint8_t intf = 0;
        if (HostCtl[0].Interface[intf].InEndpNum > 0)
        {
            uint16_t len = HostCtl[0].Interface[intf].InEndpSize[0];
            s = USBFSH_GetInEndpData(0, HostCtl[0].Interface[intf].InEndpAddr[0], &HostCtl[0].Interface[intf].InEndpTog[0], Gamepad_Report_Buf, &len);
            if (s == ERR_SUCCESS && len > 0)
            {
                if (memcmp(Gamepad_Report_Buf, Gamepad_Prev_Report_Buf, len) != 0)
                {
                    DUG_PRINTF("HID Data: ");
                    for(int i=0; i<len; i++) DUG_PRINTF("%02x ", Gamepad_Report_Buf[i]);
                    DUG_PRINTF("\r\n");
                    memcpy(Gamepad_Prev_Report_Buf, Gamepad_Report_Buf, len);
                    
                    // Toggle LED on verification
                    GPIO_WriteBit(GPIOA, GPIO_Pin_0, (GPIO_ReadOutputDataBit(GPIOA, GPIO_Pin_0) == Bit_SET) ? Bit_RESET : Bit_SET);
                }
            }
        }
        Delay_Ms(10); // Simple polling interval
    }
}

/*******************************************************************************
 * @fn      USBFS_IRQHandler
 * @brief   USBFS Interrupt Handler. (Need to be called from ch32x035_it.c)
 */
void USBFS_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void USBFS_IRQHandler(void)
{
    USBFSH_IRQHandler();
}
