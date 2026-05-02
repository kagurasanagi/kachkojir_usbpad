#include "gamepad_mapper.h"
#include "profile_common.h"
#include "usb_host_gamepad.h"  // For threshold macros

/**
 * @brief   Grapht Omni Gamepad Mapping Profile
 *          ABXYボタン (Byte 0, bits 0-3) が標準配置からずれているため変換する。
 *          変換: 0x04→0x01, 0x02→0x02, 0x08→0x04, 0x01→0x08
 */
static ReportStatus_t Profile_Grapht_omni(const uint8_t *report, uint16_t len, uint8_t *spi_out)
{
	/* Byte 0: ABXYボタンのビット並び替え (bit4-7 はそのまま) */
	uint8_t b0 = report[0];
	spi_out[0] = ((b0 & 0x04) >> 2) | /* bit2 (0x04) → bit0 (0x01) */
				 ((b0 & 0x02)) |	  /* bit1 (0x02) → bit1 (0x02) */
				 ((b0 & 0x08) >> 1) | /* bit3 (0x08) → bit2 (0x04) */
				 ((b0 & 0x01) << 3) | /* bit0 (0x01) → bit3 (0x08) */
				 ((b0 & 0xF0));		  /* bit4-7: 変換なし */

	/* Byte 1 (Buttons 9-16) */
	spi_out[1] = (report[1] & 0x0F) << 4 | (report[2] & 0x0F);

	/* Byte 2: アナログスティック → ハットスイッチ変換 */
	uint8_t ly = (report[4] < JOYSTICK_ANALOG_LOW_THRESH) ? 0 : (report[4] > JOYSTICK_ANALOG_HIGH_THRESH ? 2 : 1);
	uint8_t lx = (report[3] < JOYSTICK_ANALOG_LOW_THRESH) ? 0 : (report[3] > JOYSTICK_ANALOG_HIGH_THRESH ? 2 : 1);
	uint8_t ry = (report[6] < JOYSTICK_ANALOG_LOW_THRESH) ? 0 : (report[6] > JOYSTICK_ANALOG_HIGH_THRESH ? 2 : 1);
	uint8_t rx = (report[5] < JOYSTICK_ANALOG_LOW_THRESH) ? 0 : (report[5] > JOYSTICK_ANALOG_HIGH_THRESH ? 2 : 1);
	spi_out[2] = hat_map[ly][lx] << 4 | hat_map[ry][rx];

	return REPORT_HANDLED;
}

REGISTER_GAMEPAD_PROFILE(grapht_omni, 0x0D22, 0x0C31, Profile_Grapht_omni);
