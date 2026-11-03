# dmst7789 API

## The node

`/dev/dmst7789<instance>` (`instance` of the configuration, 0 by default). The device file *is* the framebuffer: `read()` and `write()`
at a byte offset (RGB565, little-endian in memory, `stride = width * 2`). Nothing reaches the panel until `PRESENT` or `flush()`.

| Command | |
|---|---|
| `DMDRVI_IOCTL_GFX_GET_INFO` | `dmdrvi_gfx_info_t`: the size as shown (after the rotation), RGB565, stride, 1 buffer |
| `DMDRVI_IOCTL_GFX_GET_FRAMEBUFFER` | the address of the framebuffer, to draw into directly; then `PRESENT` the area drawn |
| `DMDRVI_IOCTL_GFX_FILL_RECT` | fills a rectangle of the framebuffer (clipped; color 0xAARRGGBB, the alpha is ignored); not shown until `PRESENT` |
| `DMDRVI_IOCTL_GFX_PRESENT` | sends `area` (NULL: the whole screen) of the framebuffer to the panel |
| `DMDRVI_IOCTL_GFX_SET/GET_DISPLAY_ENABLED` | DISPON / DISPOFF (the backlight follows) |
| `DMDRVI_IOCTL_GFX_SET/GET_BACKLIGHT` | the backlight line (only lit while the display is enabled) |
| `DMDRVI_IOCTL_GFX_SWAP_BUFFERS`, `_WAIT_VSYNC` | `-ENOTSUP`: one buffer, no scan-out |
| `dmst7789_ioctl_cmd_get_config` | `dmst7789_config_t` |

`flush()` presents what was written through `write()` or `FILL_RECT` since the last `PRESENT` (the lines touched). A client that draws
through the pointer of `GET_FRAMEBUFFER` must `PRESENT` the area itself.

A `PRESENT` is a window (CASET, RASET), RAMWR and the pixels as they are in the framebuffer (the start sequence puts the panel in
little-endian pixels, RAMCTRL 0xB0), in chunks of whole lines copied into a buffer in the internal RAM (so that the DMA of dmspi moves it
in place). The whole 240x320 screen is 150 kB: on the ESP32-S3 at 40 MHz about
90 ms (31 ms on the wire; the rest is the copy and the set-up of every chunk), a partial `PRESENT` costs in proportion.

## Functions (module API)

| | |
|---|---|
| `bool dmst7789_validate_config(const dmst7789_config_t*)` | the size and the offsets fit the 240 x 320 RAM of the controller, the rotation is known |
| `uint16_t dmst7789_color_to_rgb565(uint32_t argb)` | the pixel of the framebuffer |
