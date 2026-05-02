#include "gamepad_mapper.h"

#include <string.h>

#include "spi_slave.h"		   // For updating SPI if necessary (though done in caller)
#include "usb_host_gamepad.h"  // For threshold macros and Gamepad_Raw_Report

/* Linker symbols for the gamepad_profiles section */
extern const GamepadProfile_t __start_gamepad_profiles[];
extern const GamepadProfile_t __stop_gamepad_profiles[];

/**
 * @brief Finds the appropriate gamepad profile for the given VID and PID.
 * @param vid Vendor ID of the connected device
 * @param pid Product ID of the connected device
 * @return Pointer to the matched GamepadProfile_t, or the generic fallback.
 */
const GamepadProfile_t* GamepadMapper_FindProfile(uint16_t vid, uint16_t pid)
{
	const GamepadProfile_t* profile_start = __start_gamepad_profiles;
	const GamepadProfile_t* profile_end = __stop_gamepad_profiles;

	/* First pass: Look for an exact VID and PID match */
	for (const GamepadProfile_t* p = profile_start; p < profile_end; p++)
	{
		if (p->vid == vid && p->pid == pid)
		{
			return p;
		}
	}

	/* Second pass: Look for a Vendor-only match (pid = 0x0000) */
	for (const GamepadProfile_t* p = profile_start; p < profile_end; p++)
	{
		if (p->vid == vid && p->pid == 0x0000)
		{
			return p;
		}
	}

	/* Third pass: Look for the ultimate fallback (0x0000, 0x0000) */
	for (const GamepadProfile_t* p = profile_start; p < profile_end; p++)
	{
		if (p->vid == 0x0000 && p->pid == 0x0000)
		{
			return p;
		}
	}

	/* Failsafe: if the fallback wasn't compiled in, return the first available profile, or NULL */
	return (profile_start < profile_end) ? profile_start : NULL;
}
