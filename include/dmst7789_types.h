#ifndef DMST7789_TYPES_H
#define DMST7789_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include "dmdrvi_ioctl.h"

/**
 * @brief Rotation of the picture: the MADCTL setting of the panel
 *
 * 90 and 270 swap the width and the height (width/height of the configuration are those of the panel in rotation 0).
 */
typedef enum
{
    dmst7789_rotation_0 = 0,
    dmst7789_rotation_90,
    dmst7789_rotation_180,
    dmst7789_rotation_270,
} dmst7789_rotation_t;

/**
 * @brief Configuration of a display (the ini section of the driver)
 */
typedef struct
{
    uint16_t            width;              /**< Width of the panel in rotation 0, pixels (1..240) */
    uint16_t            height;             /**< Height of the panel in rotation 0, lines (1..320) */
    uint16_t            offset_x;           /**< Column of the RAM of the controller where the panel starts */
    uint16_t            offset_y;           /**< Line of the RAM of the controller where the panel starts */
    dmst7789_rotation_t rotation;           /**< Rotation of the picture */
    bool                invert;             /**< Colors inverted (INVON): IPS panels need it */
    bool                bgr;                /**< The panel has the red and the blue the other way round */
    uint32_t            clear_color;        /**< 0xAARRGGBB the framebuffer starts with (the picture is shown from the start) */
} dmst7789_config_t;

/** Commands of the node, besides the generic graphics ones (DMDRVI_IOCTL_GFX_*) */
typedef enum
{
    dmst7789_ioctl_cmd_get_config = DMDRVI_IOCTL_CUSTOM_BASE,  /**< arg: dmst7789_config_t* */
    dmst7789_ioctl_cmd_max
} dmst7789_ioctl_cmd_t;

#endif /* DMST7789_TYPES_H */
