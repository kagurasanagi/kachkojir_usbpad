#include "gamepad_mapper.h"
#include "profile_common.h"
#include "usb_host_gamepad.h"  // For threshold macros and Gamepad_Is_Switch_Clone

/* 注意: このファイルの内容は、Switch Pro Controllerを偽装するコントローラーの
 * 異常なレポートで誤作動を起こすのを抑止するために定義されており、
 * Switch Pro Controllerの正しい挙動を完全に反映させるための物ではありません。
 */

/**
 * @brief   Nintendo Switch Pro Controller Mapping Profile
 */
static ReportStatus_t Profile_SwitchPro(const uint8_t *report, uint16_t len, uint8_t *spi_out)
{
	/* 偽物Switchコントローラーの異常レポート (0x81で始まる) を無視 */
	if (Gamepad_Is_Switch_Clone && len > 0 && report[0] == 0x81)
	{
		return REPORT_IGNORED;
	}

	/* Byte 0 and Byte 1 (Buttons) */
	spi_out[0] = report[0];
	spi_out[1] = (report[1] & 0x0F) << 4 | (report[2] & 0x0F);

	/* Byte 2: Analog Threshold Processing for Hat Simulation */
	uint8_t ly = (report[4] < JOYSTICK_ANALOG_LOW_THRESH) ? 0 : (report[4] > JOYSTICK_ANALOG_HIGH_THRESH ? 2 : 1);
	uint8_t lx = (report[3] < JOYSTICK_ANALOG_LOW_THRESH) ? 0 : (report[3] > JOYSTICK_ANALOG_HIGH_THRESH ? 2 : 1);
	uint8_t ry = (report[6] < JOYSTICK_ANALOG_LOW_THRESH) ? 0 : (report[6] > JOYSTICK_ANALOG_HIGH_THRESH ? 2 : 1);
	uint8_t rx = (report[5] < JOYSTICK_ANALOG_LOW_THRESH) ? 0 : (report[5] > JOYSTICK_ANALOG_HIGH_THRESH ? 2 : 1);

	spi_out[2] = hat_map[ly][lx] << 4 | hat_map[ry][rx];

	return REPORT_HANDLED;
}

/*
 * Register this profile
 */
REGISTER_GAMEPAD_PROFILE(switch_pro, 0x057E, 0x2009, Profile_SwitchPro);
