#ifndef DMST7789_SPLASH_H
#define DMST7789_SPLASH_H

#include "dmst7789.h"

/**
 * @brief Draws the logo of a .dmvir file in the middle of the framebuffer (the implementation of dmst7789_splash_draw).
 *
 * @return 0 on success, -ENOENT when the file does not exist, another negative errno when it is not a logo this driver can draw
 */
int dmst7789_splash_draw_file(const char *path, const dmst7789_splash_target_t *target);

#endif /* DMST7789_SPLASH_H */
