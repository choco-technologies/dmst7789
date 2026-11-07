# dmst7789 configuration

The section of the display (`driver_name=dmst7789`):

| Key | Default | |
|---|---|---|
| `instance` | 0 | the node is `/dev/dmst7789<instance>` |
| `width`, `height` | 240, 320 | the panel in rotation 0 (width <= 240, height <= 320 with the offsets) |
| `offset_x`, `offset_y` | 0 | where the panel starts in the 240 x 320 RAM of the controller (a 135x240 panel: 52 / 40) |
| `rotation` | 0 | 0, 90, 180 or 270 (90 and 270 swap the width and the height of the node) |
| `invert` | on | INVON: IPS panels need it |
| `rgb_order` | rgb | `rgb` or `bgr` (the red and the blue the other way round) |
| `clear_color` | 0xFF000000 | 0xAARRGGBB the framebuffer starts with; shown from the start - the background of the splash screen |
| `splash_logo` | `$SPLASH_LOGO` | path of the splash logo (`.dmvir`) drawn in the middle of the screen at start; `none`: no logo. See [Splash screen](#splash-screen) |
| `reset` | no | `yes`: the panel waits for a `reset` friend before it is started |
| `backlight` | on | the backlight state at the start |
| `backlight_active_level` | high | `low`: the backlight is on when its line is low |
| `display` | on | the display state at the start |

## Friends

`dmdevfs` tells every driver of a `friends_group` about the others (`friend_role=`), whatever order they are created in. The panel is
started once the required ones are there:

| `friend_role` | |
|---|---|
| `spi` | the dmspi node the display is on (required): `mode=0`, `nss_mode=soft`; 40 MHz is what the vendor's panel takes |
| `dc` | the data/command line, a dmgpio output: low for a command, high for data (required) |
| `reset` | the reset line, a dmgpio output, low = reset (with `reset=yes`) |
| `backlight` | the backlight enable, a dmgpio output (optional) |
| `chip_select` | not this driver's: dmspi drives it around every write |

Declare the display last in the file: the order is the order of the creation and, reversed, of the teardown (the display must not
outlive the nodes it writes to).

## Splash screen

The display shows a splash screen from its first frame on, the same as dmlcdtft: the framebuffer is filled with `clear_color` and a logo
is drawn in the middle (cropped when it is larger than the screen) before the panel is started, so the panel never shows garbage while
the system boots.

The logo is a `.dmvir` file: an *uncompressed* `.dmvi` (dmview's image format, see dmview's `docs/image-format.md`) - its 56-byte header
followed by the raw pixels, nothing to decode or unpack. `RGB565`, `RGB565A8` and `ARGB8888` images are drawn; transparent pixels are
blended over `clear_color`. The file is read row by row, so only a few lines of it are in memory at a time.

Where the logo comes from:

1. the `splash_logo` key, if set (`none` turns the logo off);
2. else the `SPLASH_LOGO` environment variable - dmod-boot sets it to `DMBOOT_SPLASH_LOGO_PATH` (`/eviews/splash_logo.dmvir`) and puts the
   logo picked by the board configuration there.

No logo configured, or no file at the path: just `clear_color`. A file that is not a logo the driver can draw (e.g. a packed `.dmvi`) is
logged and ignored.
