# lcdtest

Draws test pictures on a display with the generic graphics interface of dmdrvi (any `DMDRVI_IOCTL_GFX_*` node, e.g. `/dev/dmst77890`):

```
lcdtest /dev/dmst77890
```

It prints the geometry, fills the screen with red, green, blue, white and black (one second each, the time of every PRESENT is printed),
then draws the colour bars, a white frame, a gradient and a moving square, and tests the backlight and the display switch. Look at the
screen: the colours must be what the lines say.
