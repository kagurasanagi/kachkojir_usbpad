/********************************** (C) COPYRIGHT *******************************
 * File Name          : usbc_source.c
 * Description        : USB Type-C Source (CC monitoring + Load Switch control)
 *
 * Hardware:
 *   MCU    : CH32X035G8U6 (QFN28), VDD = 3.3V
 *   CC1    : PC14 (Pin 28) - Internal USBPD PHY
 *   CC2    : PC15 (Pin 1)  - Internal USBPD PHY
 *   LoadSW : PA0  (Pin 5)  - High = ON, Low = OFF
 *
 * Operation:
 *   Source mode enables Rp pull-up (330uA) on both CC1 and CC2.
 *   When a Sink device (5.1k Rd) is connected, voltage on one CC pin drops.
 *   The internal comparator (CC_CMP) is used to detect this threshold crossing.
 *   Load switch is turned ON only after debounce confirms stable attachment,
 *   and turned OFF immediately (with brief debounce) upon detachment.
 *******************************************************************************/

#include "usbc_source.h"
#include "debug.h"

/* ---------- Private State ---------- */
static USBC_SRC_State_t s_state = USBC_SRC_DISCONNECTED;
static uint8_t  s_active_cc    = 0;    /* 0: none, 1: CC1, 2: CC2 */
static uint8_t  s_debounce_cnt = 0;

/* ---------- Private Helpers ---------- */

/*
 * Load Switch ON/OFF
 */
static void LoadSwitch_On(void)
{
    GPIO_WriteBit(LOADSW_GPIO_PORT, LOADSW_GPIO_PIN, Bit_SET);
}

static void LoadSwitch_Off(void)
{
    GPIO_WriteBit(LOADSW_GPIO_PORT, LOADSW_GPIO_PIN, Bit_RESET);
}

/*
 * Read CC pin voltage level using internal comparator.
 *
 * Source presents Rp (330uA pull-up).
 *   - Open (no device):  CC voltage ~ VDD (≈ 3.3V) → above all thresholds
 *   - Rd connected (Sink): CC voltage ≈ 330uA × 5.1kΩ ≈ 1.68V
 *     → above 0.66V, above 0.95V, above 1.23V, below 2.2V (GPIO threshold)
 *   - Ra connected (Audio/VCONN): CC voltage very low ≈ 330uA × 1kΩ ≈ 0.33V
 *     → above 0.22V, below 0.45V
 *
 * We detect Rd by checking:
 *   CC_CMP_66  (0.66V) → PA_CC_AI should be HIGH  (voltage > 0.66V)
 *   GPIO INDR  (2.2V)  → should be LOW             (voltage < 2.2V)
 *
 * Returns: 0 = Open/No device, 1 = Rd detected (Sink), 2 = Ra detected
 */
static uint8_t CC_Read_State(volatile uint16_t *port_cc_reg, uint16_t gpio_pin)
{
    uint8_t cmp_result = 0;

    /* Test with CC_CMP_22 (0.22V threshold) */
    *port_cc_reg &= ~(CC_CMP_Mask | PA_CC_AI);
    *port_cc_reg |= CC_CMP_22;
    Delay_Us(2);
    if (*port_cc_reg & PA_CC_AI)
    {
        cmp_result |= bCC_CMP_22;      /* Voltage > 0.22V */
    }

    /* Test with CC_CMP_66 (0.66V threshold) */
    *port_cc_reg &= ~(CC_CMP_Mask | PA_CC_AI);
    *port_cc_reg |= CC_CMP_66;
    Delay_Us(2);
    if (*port_cc_reg & PA_CC_AI)
    {
        cmp_result |= bCC_CMP_66;      /* Voltage > 0.66V */
    }

    /* Restore default comparator setting */
    *port_cc_reg &= ~(CC_CMP_Mask | PA_CC_AI);
    *port_cc_reg |= CC_CMP_66;

    /* Check GPIO (high threshold ≈ 2.2V) */
    uint8_t gpio_high = 0;
    if ((GPIOC->INDR & gpio_pin) != (uint32_t)Bit_RESET)
    {
        gpio_high = 1;                 /* Voltage > 2.2V → Open */
    }

    /*
     * Decision logic (Source mode, Rp = 330uA):
     *
     *   Open   : > 2.2V  → gpio_high=1
     *   Rd(5.1k): ~ 1.68V → cmp_66=1, gpio_high=0
     *   Ra(1k)  : ~ 0.33V → cmp_22=1, cmp_66=0
     */
    if (gpio_high)
    {
        return 0;   /* Open - no device */
    }
    else if (cmp_result & bCC_CMP_66)
    {
        return 1;   /* Rd detected → Sink device */
    }
    else if (cmp_result & bCC_CMP_22)
    {
        return 2;   /* Ra detected → Audio adapter or VCONN */
    }
    else
    {
        return 0;   /* Below 0.22V - treat as no connection */
    }
}

/* ---------- Public API ---------- */

/*
 * @fn      USBC_Source_Init
 * @brief   Initialize USBPD PHY for Source mode and configure load switch GPIO.
 *
 * - Enables Rp (330uA) pull-up on both CC1 (PC14) and CC2 (PC15).
 * - Configures load switch GPIO (PA0) as output, initially OFF.
 * - Does NOT enable PD communication (BMC), only CC-level detection.
 */
void USBC_Source_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    /* --- Load Switch GPIO --- */
    RCC_APB2PeriphClockCmd(LOADSW_GPIO_CLK, ENABLE);
    GPIO_InitStructure.GPIO_Pin   = LOADSW_GPIO_PIN;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(LOADSW_GPIO_PORT, &GPIO_InitStructure);
    LoadSwitch_Off();

    /* --- CC1/CC2 GPIO (PC14, PC15) as Floating Input --- */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_14 | GPIO_Pin_15;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_IN_FLOATING;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOC, &GPIO_InitStructure);

    /* --- USBPD PHY Clock & Config --- */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_USBPD, ENABLE);

    /* VDD = 3.3V → USBPD_PHY_V33 = 1 (direct VDD, no LDO)
     * High threshold input: USBPD_IN_HVT = 1 (2.2V typical) */
    AFIO->CTLR |= USBPD_IN_HVT | USBPD_PHY_V33;

    /* Enable PD PHY (required for analog section: pull-up current source & comparators) */
    USBPD->CONFIG = PD_DMA_EN;

    /* Clear all PD interrupt flags */
    USBPD->STATUS = BUF_ERR | IF_RX_BIT | IF_RX_BYTE | IF_RX_ACT | IF_RX_RESET | IF_TX_END;

    /* --- Source Mode: Enable Rp (330uA) on both CC lines --- */
    /* CC_CMP_66 as default comparator threshold for detection */
    USBPD->PORT_CC1 = CC_CMP_66 | CC_PU_330;
    USBPD->PORT_CC2 = CC_CMP_66 | CC_PU_330;

    /* Reset state */
    s_state        = USBC_SRC_DISCONNECTED;
    s_active_cc    = 0;
    s_debounce_cnt = 0;

    /* Debug: Verify register values */
    printf("USBC Source Init OK (Rp=330uA on CC1/CC2)\r\n");
    printf("  AFIO->CTLR = 0x%08lx\r\n", AFIO->CTLR);
    printf("  USBPD->CONFIG   = 0x%04x\r\n", USBPD->CONFIG);
    printf("  USBPD->PORT_CC1 = 0x%04x\r\n", USBPD->PORT_CC1);
    printf("  USBPD->PORT_CC2 = 0x%04x\r\n", USBPD->PORT_CC2);
}

/*
 * @fn      USBC_Source_Detect
 * @brief   Periodic CC detection. Call every ~4ms from main loop or timer.
 *
 * State machine:
 *   DISCONNECTED → detect Rd on CC1 or CC2 → debounce → ATTACHED (LoadSW ON)
 *   ATTACHED     → detect Open on active CC  → debounce → DISCONNECTED (LoadSW OFF)
 */
void USBC_Source_Detect(void)
{
    uint8_t cc1_state, cc2_state;

    if (s_state == USBC_SRC_DISCONNECTED)
    {
        /* --- Detect Attachment --- */
        cc1_state = CC_Read_State(&USBPD->PORT_CC1, PIN_CC1);
        cc2_state = CC_Read_State(&USBPD->PORT_CC2, PIN_CC2);

        uint8_t candidate = 0;
        if (cc1_state == 1)       candidate = 1;  /* Rd on CC1 */
        else if (cc2_state == 1)  candidate = 2;  /* Rd on CC2 */

        if (candidate)
        {
            s_debounce_cnt++;
            if (s_debounce_cnt >= CC_DEBOUNCE_ATTACH)
            {
                s_debounce_cnt = 0;
                s_active_cc = candidate;
                s_state = USBC_SRC_ATTACHED;
                LoadSwitch_On();
                printf("USBC: Sink Attached (CC%d)\r\n", s_active_cc);
            }
        }
        else
        {
            s_debounce_cnt = 0;
        }
    }
    else /* USBC_SRC_ATTACHED */
    {
        /* --- Detect Detachment on active CC only --- */
        uint8_t active_state;
        if (s_active_cc == 1)
        {
            active_state = CC_Read_State(&USBPD->PORT_CC1, PIN_CC1);
        }
        else
        {
            active_state = CC_Read_State(&USBPD->PORT_CC2, PIN_CC2);
        }

        if (active_state != 1)  /* No longer Rd */
        {
            s_debounce_cnt++;
            if (s_debounce_cnt >= CC_DEBOUNCE_DETACH)
            {
                s_debounce_cnt = 0;
                LoadSwitch_Off();
                printf("USBC: Sink Detached (was CC%d)\r\n", s_active_cc);
                s_active_cc = 0;
                s_state = USBC_SRC_DISCONNECTED;
            }
        }
        else
        {
            s_debounce_cnt = 0;
        }
    }
}

USBC_SRC_State_t USBC_Source_GetState(void)
{
    return s_state;
}

uint8_t USBC_Source_GetActiveCC(void)
{
    return s_active_cc;
}
