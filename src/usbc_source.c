/********************************** (C) COPYRIGHT
 ******************************** ファイル名          : usbc_source.c 説明 : USB
 *Type-C ソース (CC モニタリング + ロードスイッチ 制御)
 *
 * ハードウェア:
 *   MCU    : CH32X035G8U6 (QFN28), VDD = 3.3V
 *   CC1    : PC14 (Pin 28) - 内部 USBPD PHY
 *   CC2    : PC15 (Pin 1)  - 内部 USBPD PHY
 *   LoadSW : PA1  (Pin 6)  - Low = ON, High = OFF (/EN)
 *
 * 動作:
 *   ソースモード では CC1 と CC2 の両方で Rp プルアップ (330uA)
 *を有効にします。 シンクデバイス (5.1k Rd) が 接続 されると、一方の CC ピン の
 *電圧 が低下します。 内部 コンパレータ (CC_CMP) を使用して、この スレッショルド
 *の交差を 検出 します。 ロードスイッチ は、デバウンス によって安定した
 *アタッチメント が確認された後にのみ ON になります。 デタッチメント 時には
 *(短い デバウンス の後) 即座に OFF になります。
 *******************************************************************************/

#include "usbc_source.h"
#include "debug.h"

/* ---------- プライベート ステート ---------- */
static USBC_SRC_State_t s_state = USBC_SRC_DISCONNECTED;
static uint8_t s_active_cc = 0; /* 0: なし, 1: CC1, 2: CC2 */
static uint8_t s_debounce_cnt = 0;
static uint8_t s_oc_latched = 0; /* 1 = 過電流検出済み (再起動まで維持) */

/* ---------- プライベート ヘルパー ---------- */

/*
 * ロードスイッチ ON/OFF
 */
static void LoadSwitch_On(void) {
  if (s_oc_latched)
    return; /* 過電流状態では ON にしない */
  GPIO_WriteBit(LOADSW_GPIO_PORT, LOADSW_GPIO_PIN, Bit_RESET);
}

static void LoadSwitch_Off(void) {
  GPIO_WriteBit(LOADSW_GPIO_PORT, LOADSW_GPIO_PIN, Bit_SET);
}

/*
 * 内部 コンパレータ を使用して CC ピン の 電圧 レベル を読み取ります。
 *
 * ソース は Rp (330uA プルアップ) を提示します。
 *   - オープン (デバイスなし): CC 電圧 ≈ VDD (≈ 3.3V) → すべての スレッショルド
 * を上回る
 *   - Rd 接続 (シンク): CC 電圧 ≈ 330uA × 5.1kΩ ≈ 1.68V
 *     → 0.66V 以上, 0.95V 以上, 1.23V 以上, 2.2V 未満 (GPIO スレッショルド)
 *   - Ra 接続 (オーディオ/VCONN): CC 電圧 は非常に低い ≈ 330uA × 1kΩ ≈ 0.33V
 *     → 0.22V 以上, 0.45V 未満
 *
 * 以下のチェックにより Rd を 検出 します:
 *   CC_CMP_66  (0.66V) → PA_CC_AI が HIGH であること (電圧 > 0.66V)
 *   GPIO INDR  (2.2V)  → LOW であること             (電圧 < 2.2V)
 *
 * 戻り値: 0 = オープン/デバイスなし, 1 = Rd 検出 (シンク), 2 = Ra 検出
 */
static uint8_t CC_Read_State(volatile uint16_t *port_cc_reg,
                             uint16_t gpio_pin) {
  uint8_t cmp_result = 0;

  /* CC_CMP_22 (0.22V スレッショルド) でテスト */
  *port_cc_reg &= ~(CC_CMP_Mask | PA_CC_AI);
  *port_cc_reg |= CC_CMP_22;
  Delay_Us(2);
  if (*port_cc_reg & PA_CC_AI) {
    cmp_result |= bCC_CMP_22; /* 電圧 > 0.22V */
  }

  /* CC_CMP_66 (0.66V スレッショルド) でテスト */
  *port_cc_reg &= ~(CC_CMP_Mask | PA_CC_AI);
  *port_cc_reg |= CC_CMP_66;
  Delay_Us(2);
  if (*port_cc_reg & PA_CC_AI) {
    cmp_result |= bCC_CMP_66; /* 電圧 > 0.66V */
  }

  /* デフォルト の コンパレータ 設定を復元 */
  *port_cc_reg &= ~(CC_CMP_Mask | PA_CC_AI);
  *port_cc_reg |= CC_CMP_66;

  /* GPIO をチェック (高い スレッショルド ≈ 2.2V) */
  uint8_t gpio_high = 0;
  if ((GPIOC->INDR & gpio_pin) != (uint32_t)Bit_RESET) {
    gpio_high = 1; /* 電圧 > 2.2V → オープン */
  }

  /*
   * 判定 ロジック (ソースモード, Rp = 330uA):
   *
   *   オープン : > 2.2V  → gpio_high=1
   *   Rd(5.1k): ~ 1.68V → cmp_66=1, gpio_high=0
   *   Ra(1k)  : ~ 0.33V → cmp_22=1, cmp_66=0
   */
  if (gpio_high) {
    return 0; /* オープン - デバイスなし */
  } else if (cmp_result & bCC_CMP_66) {
    return 1; /* Rd 検出 → シンクデバイス */
  } else if (cmp_result & bCC_CMP_22) {
    return 2; /* Ra 検出 → オーディオアダプタ または VCONN */
  } else {
    return 0; /* 0.22V 未満 - 非接続として扱う */
  }
}

/* ---------- パブリック API ---------- */

/*
 * @fn      USBC_Source_Init
 * @brief   ソースモード 用に USBPD PHY を初期化し、ロードスイッチ GPIO
 * を設定します。
 *
 * - CC1 (PC14) と CC2 (PC15) の両方で Rp (330uA) プルアップ を有効にします。
 * - ロードスイッチ GPIO (PA1) を 出力 として初期状態 OFF に設定します。
 * - 過電流検知ピン (PA0) を入力として設定します。
 * - 異常表示 LED (PC3) を出力として設定します。
 */
void USBC_Source_Init(void) {
  GPIO_InitTypeDef GPIO_InitStructure = {0};

  /* --- ロードスイッチ GPIO --- */
  RCC_APB2PeriphClockCmd(LOADSW_GPIO_CLK, ENABLE);
  GPIO_InitStructure.GPIO_Pin = LOADSW_GPIO_PIN;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
  GPIO_Init(LOADSW_GPIO_PORT, &GPIO_InitStructure);
  LoadSwitch_Off();

  /* 過電流検知 GPIO (PA0) - 外部プルアップを使用するため内部プルは使用しない */
  RCC_APB2PeriphClockCmd(OC_GPIO_CLK, ENABLE);
  GPIO_InitStructure.GPIO_Pin = OC_GPIO_PIN;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
  GPIO_Init(OC_GPIO_PORT, &GPIO_InitStructure);

  /* 異常表示 LED (PC3) - プッシュプル出力 */
  RCC_APB2PeriphClockCmd(FAULT_LED_GPIO_CLK, ENABLE);
  GPIO_InitStructure.GPIO_Pin = FAULT_LED_GPIO_PIN;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
  GPIO_Init(FAULT_LED_GPIO_PORT, &GPIO_InitStructure);
  GPIO_WriteBit(FAULT_LED_GPIO_PORT, FAULT_LED_GPIO_PIN, Bit_RESET);

  /* --- CC1/CC2 GPIO (PC14, PC15) を フローティング入力 として設定 --- */
  RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);
  GPIO_InitStructure.GPIO_Pin = GPIO_Pin_14 | GPIO_Pin_15;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
  GPIO_Init(GPIOC, &GPIO_InitStructure);

  /* --- USBPD PHY クロック & コンフィグ --- */
  RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);
  RCC_AHBPeriphClockCmd(RCC_AHBPeriph_USBPD, ENABLE);

  /* VDD = 3.3V → USBPD_PHY_V33 = 1 (直接 VDD, LDOなし)
   * 高 スレッショルド 入力: USBPD_IN_HVT = 1 (標準 2.2V) */
  AFIO->CTLR |= USBPD_IN_HVT | USBPD_PHY_V33;

  /* PD PHY を有効化 (アナログ セクション: プルアップ 電流源 & コンパレータ
   * に必要) */
  USBPD->CONFIG = PD_DMA_EN;

  /* すべての PD インタラプトフラグ をクリア */
  USBPD->STATUS =
      BUF_ERR | IF_RX_BIT | IF_RX_BYTE | IF_RX_ACT | IF_RX_RESET | IF_TX_END;

  /* --- ソースモード: 両方の CC ライン で Rp (330uA) を有効化 --- */
  /* 検出 用の デフォルト コンパレータ スレッショルド として CC_CMP_66 を設定 */
  USBPD->PORT_CC1 = CC_CMP_66 | CC_PU_330;
  USBPD->PORT_CC2 = CC_CMP_66 | CC_PU_330;

  /* ステート をリセット */
  s_state = USBC_SRC_DISCONNECTED;
  s_active_cc = 0;
  s_debounce_cnt = 0;
  s_oc_latched = 0;

  /* デバッグ: レジスタ 値を確認 */
  printf("USBC Source Init OK (Rp=330uA on CC1/CC2)\r\n");
}

/*
 * @fn      USBC_Source_Detect
 * @brief   周期的な CC 検出。メインループ または タイマー から約 4ms
 * ごとに呼び出します。
 *
 * ステートマシン:
 *   DISCONNECTED → CC1 または CC2 で Rd を 検出 → デバウンス → ATTACHED
 * (ロードスイッチ ON) ATTACHED     → アクティブ な CC で オープン を 検出 →
 * デバウンス → DISCONNECTED (ロードスイッチ OFF)
 */
void USBC_Source_Detect(void) {
  uint8_t cc1_state, cc2_state;

  if (s_state == USBC_SRC_DISCONNECTED) {
    /* --- アタッチメント 検出 --- */
    cc1_state = CC_Read_State(&USBPD->PORT_CC1, PIN_CC1);
    cc2_state = CC_Read_State(&USBPD->PORT_CC2, PIN_CC2);

    uint8_t candidate = 0;
    if (cc1_state == 1)
      candidate = 1; /* CC1 で Rd */
    else if (cc2_state == 1)
      candidate = 2; /* CC2 で Rd */

    if (candidate) {
      s_debounce_cnt++;
      if (s_debounce_cnt >= CC_DEBOUNCE_ATTACH) {
        s_debounce_cnt = 0;
        s_active_cc = candidate;
        s_state = USBC_SRC_ATTACHED;
        LoadSwitch_On();
        printf("USBC: Sink Attached (CC%d)\r\n", s_active_cc);
      }
    } else {
      s_debounce_cnt = 0;
    }
  } else /* USBC_SRC_ATTACHED */
  {
    /* --- アクティブ な CC でのみ デタッチメント を 検出 --- */
    uint8_t active_state;
    if (s_active_cc == 1) {
      active_state = CC_Read_State(&USBPD->PORT_CC1, PIN_CC1);
    } else {
      active_state = CC_Read_State(&USBPD->PORT_CC2, PIN_CC2);
    }

    if (active_state != 1) /* もはや Rd ではない */
    {
      s_debounce_cnt++;
      if (s_debounce_cnt >= CC_DEBOUNCE_DETACH) {
        s_debounce_cnt = 0;
        LoadSwitch_Off();
        printf("USBC: Sink Detached (was CC%d)\r\n", s_active_cc);
        s_active_cc = 0;
        s_state = USBC_SRC_DISCONNECTED;
      }
    } else {
      s_debounce_cnt = 0;
    }
  }
}

USBC_SRC_State_t USBC_Source_GetState(void) { return s_state; }

uint8_t USBC_Source_GetActiveCC(void) { return s_active_cc; }

void USBC_Source_HandleOC(void) {
  /* 最優先で遮断 */
  LoadSwitch_Off();
  s_oc_latched = 1;

  /* 異常 LED 点灯 */
  GPIO_WriteBit(FAULT_LED_GPIO_PORT, FAULT_LED_GPIO_PIN, Bit_SET);

  /* ログ出力 (割り込み内なので簡潔に) */
  printf("\r\n!!! OVER-CURRENT DETECTED (PA0) - SYSTEM LATCHED OFF !!!\r\n");
}

uint8_t USBC_Source_IsFault(void) { return s_oc_latched; }
