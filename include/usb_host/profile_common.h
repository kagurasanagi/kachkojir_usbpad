#ifndef __PROFILE_COMMON_H
#define __PROFILE_COMMON_H

#include <stdint.h>
#include "gamepad_mapper.h"

/* [Y][X] Hat Switch encoding for SPI Byte 2 (4-bit nibble)
 * Y: 0=Up, 1=Center, 2=Down
 * X: 0=Left, 1=Center, 2=Right
 * Value 0xF = neutral (no direction) */
static const uint8_t hat_map[3][3] = {
    {0x7, 0x0, 0x1},
    {0x6, 0xF, 0x2},
    {0x5, 0x4, 0x3}
};

#endif /* __PROFILE_COMMON_H */
