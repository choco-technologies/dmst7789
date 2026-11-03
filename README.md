# dmst7789

DMOD driver for displays with an **ST7789** controller on SPI (240x320 at most, RGB565).

[![CI](https://github.com/choco-technologies/dmst7789/actions/workflows/ci.yml/badge.svg)](https://github.com/choco-technologies/dmst7789/actions/workflows/ci.yml)

The controller keeps the picture in a RAM of its own, so there is no scan-out. The driver keeps a **framebuffer** (RGB565 in memory;
the heap puts it in the external RAM where there is one) that clients draw into, and `PRESENT` sends the area drawn to the panel.
The node follows the generic graphics interface of [dmdrvi](https://github.com/choco-technologies/dmdrvi) (`DMDRVI_IOCTL_GFX_*`),
as [dmlcdtft](https://github.com/choco-technologies/dmlcdtft) does, so generic code (a GUI library, a test tool) needs only the path:
`/dev/dmst77890`.

It talks to the panel through the nodes of other drivers - [dmspi](https://github.com/choco-technologies/dmspi) for the bus and
[dmgpio](https://github.com/choco-technologies/dmgpio) for the data/command, reset and backlight lines - so it has no hardware port
and runs wherever those do. The pixels go out by the DMA of dmspi where it has one.

## Use

One `friends_group` holds the display and everything it is wired to; `friend_role` says who is who (`spi`, `dc`, `reset`,
`backlight`; `chip_select` is the business of dmspi). [configs/board/esp32s3-lcd28/display.ini](configs/board/esp32s3-lcd28/display.ini)
is the whole display of the Waveshare ESP32-S3-Touch-LCD-2.8. In the flash modules of the board:

```text
dmdma driver=mcu/esp32s3.ini
dmgpio
dmspi
dmst7789 driver=board/esp32s3-lcd28/display.ini
```

```c
void *fp = Dmod_FileOpen("/dev/dmst77890", "r+");
dmdrvi_gfx_info_t info;
Dmod_Ioctl(fp, DMDRVI_IOCTL_GFX_GET_INFO, &info);            /* 240x320, RGB565 */
dmdrvi_gfx_fill_rect_t rect = { 0, 0, 240, 40, 0xFFFF0000U }; /* 0xAARRGGBB */
Dmod_Ioctl(fp, DMDRVI_IOCTL_GFX_FILL_RECT, &rect);
Dmod_Ioctl(fp, DMDRVI_IOCTL_GFX_PRESENT, NULL);              /* to the panel (or an area of it) */
```

See [docs/api-reference.md](docs/api-reference.md) and [docs/configuration.md](docs/configuration.md). `tools/lcdtest` draws test pictures.

## Building

The code does not depend on the CPU, so there is no port: the module is built per architecture, as dmtty or dmspiflash are.

```bash
cmake -S . -B build && cmake --build build && ctest --test-dir build                       # the host: the tests
cmake -S . -B build-esp -DDMOD_TOOLS_NAME=arch/xtensa/esp32s3 && cmake --build build-esp   # an architecture
```

`DMOD_CPU_FAMILY` (stm32f7 by default) only picks the package of dmgpio whose headers are used (its packages are named by family;
the headers are the same for every architecture).
