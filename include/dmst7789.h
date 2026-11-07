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

/**
 * @brief Draw the logo of a .dmvir file in the middle of a framebuffer
 *
 * A .dmvir is an uncompressed .dmvi (dmview's image format): the 56-byte header and the raw pixels - RGB565, RGB565A8 or ARGB8888.
 * Transparent pixels are blended over target->background; a logo larger than the framebuffer is cropped. The file is read row by
 * row. The driver calls it for the splash screen; it is exported for the tests and for code that draws a logo itself.
 *
 * @return 0 on success, -ENOENT when the file does not exist, another negative errno when it is not a logo it can draw
 */
dmod_dmst7789_api(1.0, int, _splash_draw, ( const char *path, const dmst7789_splash_target_t *target ));

#endif // DMST7789_H
