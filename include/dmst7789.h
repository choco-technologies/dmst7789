#ifndef DMST7789_H
#define DMST7789_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "dmod_types.h"
#include "dmst7789_defs.h"
#include "dmst7789_types.h"

/**
 * @brief Check a configuration without touching hardware
 *
 * @return true when the size, the offsets and the rotation make a panel the controller can show.
 */
dmod_dmst7789_api(1.0, bool, _validate_config, ( const dmst7789_config_t *config ));

/**
 * @brief Convert a 0xAARRGGBB color to the pixel of the framebuffer (RGB565)
 */
dmod_dmst7789_api(1.0, uint16_t, _color_to_rgb565, ( uint32_t argb ));

#endif // DMST7789_H
