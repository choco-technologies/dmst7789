#include "dmod.h"
#include "dmdrvi_ioctl.h"
#include <string.h>

static unsigned now_ms(void)
{
    return (unsigned)Dmod_GetUptime();
}

static int fill_and_present(void *fp, uint32_t color, const char *name, const dmdrvi_gfx_info_t *info)
{
    dmdrvi_gfx_fill_rect_t rect = { 0, 0, info->width, info->height, color };
    int ret = Dmod_Ioctl(fp, DMDRVI_IOCTL_GFX_FILL_RECT, &rect);
    if (ret != 0)
        return ret;
    unsigned started = now_ms();
    ret = Dmod_Ioctl(fp, DMDRVI_IOCTL_GFX_PRESENT, NULL);
    Dmod_Printf("%-6s PRESENT of the whole screen: ret %d, %u ms\n", name, ret, now_ms() - started);
    return ret;
}

static void pause_ms(unsigned ms)
{
    unsigned start = now_ms();
    while (now_ms() - start < ms)
        ;
}

static uint16_t rgb565(uint32_t r, uint32_t g, uint32_t b)
{
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        Dmod_Printf("Usage: lcdtest <display node>   e.g. lcdtest /dev/dmst77890\n");
        return 1;
    }
    void *fp = Dmod_FileOpen(argv[1], "r+");
    if (fp == NULL)
    {
        Dmod_Printf("lcdtest: cannot open '%s'\n", argv[1]);
        return 1;
    }

    dmdrvi_gfx_info_t info;
    if (Dmod_Ioctl(fp, DMDRVI_IOCTL_GFX_GET_INFO, &info) != 0)
    {
        Dmod_Printf("lcdtest: not a graphics node\n");
        Dmod_FileClose(fp);
        return 1;
    }
    Dmod_Printf("%ux%u, format %d, %u bytes per pixel, stride %u, %u buffer(s)\n", info.width, info.height, (int)info.pixel_format,
                info.bytes_per_pixel, (unsigned)info.stride, info.buffer_count);

    int failed = 0;
    failed += fill_and_present(fp, 0xFFFF0000U, "red", &info) != 0;
    pause_ms(1000);
    failed += fill_and_present(fp, 0xFF00FF00U, "green", &info) != 0;
    pause_ms(1000);
    failed += fill_and_present(fp, 0xFF0000FFU, "blue", &info) != 0;
    pause_ms(1000);
    failed += fill_and_present(fp, 0xFFFFFFFFU, "white", &info) != 0;
    pause_ms(1000);
    failed += fill_and_present(fp, 0xFF000000U, "black", &info) != 0;

    /* colour bars, drawn straight into the framebuffer */
    void *fb = NULL;
    if (Dmod_Ioctl(fp, DMDRVI_IOCTL_GFX_GET_FRAMEBUFFER, &fb) == 0 && fb != NULL && info.pixel_format == DMDRVI_GFX_PIXEL_FORMAT_RGB565)
    {
        static const uint32_t bars[8][3] = { {255,255,255}, {255,255,0}, {0,255,255}, {0,255,0}, {255,0,255}, {255,0,0}, {0,0,255}, {0,0,0} };
        for (unsigned y = 0; y < info.height; y++)
        {
            uint16_t *line = (uint16_t *)((uint8_t *)fb + (uint32_t)y * info.stride);
            for (unsigned x = 0; x < info.width; x++)
            {
                if (y < info.height * 3U / 4U)
                {
                    const uint32_t *c = bars[x * 8U / info.width];
                    line[x] = rgb565(c[0], c[1], c[2]);
                }
                else
                {
                    uint32_t v = x * 255U / (info.width - 1U);       /* the gradient: red, green, blue thirds of the lower part */
                    unsigned third = (y - info.height * 3U / 4U) * 3U / (info.height - info.height * 3U / 4U);
                    line[x] = third == 0 ? rgb565(v, 0, 0) : third == 1 ? rgb565(0, v, 0) : rgb565(0, 0, v);
                }
            }
        }
        dmdrvi_gfx_fill_rect_t frame_top = { 0, 0, info.width, 2, 0xFFFFFFFFU };
        dmdrvi_gfx_fill_rect_t frame_left = { 0, 0, 2, info.height, 0xFFFFFFFFU };
        (void)Dmod_Ioctl(fp, DMDRVI_IOCTL_GFX_FILL_RECT, &frame_top);
        (void)Dmod_Ioctl(fp, DMDRVI_IOCTL_GFX_FILL_RECT, &frame_left);
        unsigned started = now_ms();
        int ret = Dmod_Ioctl(fp, DMDRVI_IOCTL_GFX_PRESENT, NULL);
        Dmod_Printf("bars   PRESENT of the whole screen: ret %d, %u ms\n", ret, now_ms() - started);
        failed += ret != 0;
        pause_ms(2000);

        /* a moving square: a partial PRESENT of the old and the new place */
        for (unsigned step = 0; step < 20; step++)
        {
            dmdrvi_gfx_fill_rect_t clear = { (uint16_t)(step * 6U), 100, 40, 40, 0xFF000000U };
            dmdrvi_gfx_fill_rect_t square = { (uint16_t)((step + 1U) * 6U), 100, 40, 40, 0xFFFF8000U };
            dmdrvi_gfx_rect_t area = { (uint16_t)(step * 6U), 100, 46, 40 };
            (void)Dmod_Ioctl(fp, DMDRVI_IOCTL_GFX_FILL_RECT, &clear);
            (void)Dmod_Ioctl(fp, DMDRVI_IOCTL_GFX_FILL_RECT, &square);
            failed += Dmod_Ioctl(fp, DMDRVI_IOCTL_GFX_PRESENT, &area) != 0;
        }
        Dmod_Printf("moving square done\n");
    }

    bool off = false, on = true;
    pause_ms(1000);
    failed += Dmod_Ioctl(fp, DMDRVI_IOCTL_GFX_SET_BACKLIGHT, &off) != 0;
    pause_ms(1000);
    failed += Dmod_Ioctl(fp, DMDRVI_IOCTL_GFX_SET_BACKLIGHT, &on) != 0;
    Dmod_Printf("backlight off and on\n");

    Dmod_FileClose(fp);
    Dmod_Printf("lcdtest: %d failed step(s)\n", failed);
    return failed;
}
