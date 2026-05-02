#ifndef __GAMEPAD_MAPPER_H
#define __GAMEPAD_MAPPER_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>

	/**
	 * @brief Report processing status
	 */
	typedef enum
	{
		REPORT_HANDLED = 0, /* Report was successfully parsed and mapped */
		REPORT_IGNORED		/* Report is transitional/abnormal and should be ignored */
	} ReportStatus_t;

	/**
	 * @brief Function pointer type for processing a gamepad report
	 *
	 * @param report      Pointer to raw HID report data
	 * @param len         Length of the report
	 * @param spi_out     Pointer to the 3-byte SPI output buffer
	 * @return ReportStatus_t REPORT_HANDLED if valid, REPORT_IGNORED to skip
	 */
	typedef ReportStatus_t (*GamepadProcessFunc)(const uint8_t *report, uint16_t len, uint8_t *spi_out);

	/**
	 * @brief Gamepad Profile Structure
	 */
	typedef struct
	{
		uint16_t vid;				/* Vendor ID (0x0000 = ANY/Fallback) */
		uint16_t pid;				/* Product ID (0x0000 = ANY/Fallback) */
		GamepadProcessFunc process; /* Pointer to the mapping function */
	} GamepadProfile_t;

/**
 * @brief Macro to automatically register a gamepad profile.
 *        Places the structure in the "gamepad_profiles" section.
 *        The linker gathers these and provides __start_gamepad_profiles and __stop_gamepad_profiles.
 */
#define REGISTER_GAMEPAD_PROFILE(name, _vid, _pid, _process) \
	const GamepadProfile_t __profile_##name                  \
		__attribute__((used, section("gamepad_profiles"))) = {.vid = _vid, .pid = _pid, .process = _process}

	/* Public API */
	const GamepadProfile_t *GamepadMapper_FindProfile(uint16_t vid, uint16_t pid);

#ifdef __cplusplus
}
#endif

#endif /* __GAMEPAD_MAPPER_H */
