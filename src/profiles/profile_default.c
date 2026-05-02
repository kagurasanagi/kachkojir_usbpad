#include "gamepad_mapper.h"
#include "profile_common.h"
#include "usb_host_gamepad.h"  // For threshold macros

/**
 * @brief   Default Generic Gamepad Mapping Profile
 *          Maps standard HID report layout: bytes 0-2 buttons, bytes 3-6 analog sticks.
 */
static ReportStatus_t Profile_DefaultGeneric(const uint8_t *report, uint16_t len, uint8_t *spi_out)
{
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
 * Register this profile as the ultimate fallback (VID=0x0000, PID=0x0000)
 *
 * REGISTER_GAMEPAD_PROFILE の引数の意味：
 * 1. name    : プロファイルの内部登録名。他のプロファイルと被らないユニークな名前を指定（例: default_generic, ps4_v1
 * など）
 * 2. vid     : 対象コントローラーの Vendor ID (USB VID)。0x0000
 * を指定すると「全てのベンダー」にマッチ（フォールバック用）
 * 3. pid     : 対象コントローラーの Product ID (USB PID)。0x0000 を指定すると「全てのプロダクト」にマッチ
 * 4. process : 実際にデータのマッピング処理を行う関数ポインタ（上記で定義した関数名）
 */
REGISTER_GAMEPAD_PROFILE(default_generic, 0x0000, 0x0000, Profile_DefaultGeneric);
