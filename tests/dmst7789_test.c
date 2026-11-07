#define DMOD_ENABLE_REGISTRATION ON
#include "dmod_test.h"
#include "dmst7789.h"
#include <string.h>

static dmst7789_config_t panel(void)
{
    dmst7789_config_t c = { .width = 240, .height = 320, .rotation = dmst7789_rotation_0 };
    return c;
}

DMOD_TEST_STEP(validate_accepts_the_full_panel)
{
    dmst7789_config_t c = panel();
    DMOD_TEST_EXPECT_TRUE(dmst7789_validate_config(&c));
}

DMOD_TEST_STEP(validate_accepts_a_window_of_the_ram)
{
    /* the 135 x 240 panel sits at 52 / 40 in the RAM of the controller */
    dmst7789_config_t c = { .width = 135, .height = 240, .offset_x = 52, .offset_y = 40 };
    DMOD_TEST_EXPECT_TRUE(dmst7789_validate_config(&c));
}

DMOD_TEST_STEP(validate_rejects_bad_configurations)
{
    dmst7789_config_t c = panel();
    DMOD_TEST_EXPECT_FALSE(dmst7789_validate_config(NULL));
    c.width = 0;
    DMOD_TEST_EXPECT_FALSE(dmst7789_validate_config(&c));
    c = panel();
    c.height = 321;
    DMOD_TEST_EXPECT_FALSE(dmst7789_validate_config(&c));
    c = panel();
    c.width = 240;
    c.offset_x = 1;                         /* does not fit the 240 columns of the RAM */
    DMOD_TEST_EXPECT_FALSE(dmst7789_validate_config(&c));
    c = panel();
    c.rotation = (dmst7789_rotation_t)4;
    DMOD_TEST_EXPECT_FALSE(dmst7789_validate_config(&c));
}

DMOD_TEST_STEP(color_to_rgb565)
{
    DMOD_TEST_EXPECT_EQ(dmst7789_color_to_rgb565(0xFF000000U), 0x0000);
    DMOD_TEST_EXPECT_EQ(dmst7789_color_to_rgb565(0xFFFFFFFFU), 0xFFFF);
    DMOD_TEST_EXPECT_EQ(dmst7789_color_to_rgb565(0xFFFF0000U), 0xF800);
    DMOD_TEST_EXPECT_EQ(dmst7789_color_to_rgb565(0xFF00FF00U), 0x07E0);
    DMOD_TEST_EXPECT_EQ(dmst7789_color_to_rgb565(0xFF0000FFU), 0x001F);
    DMOD_TEST_EXPECT_EQ(dmst7789_color_to_rgb565(0x00808080U), 0x8410);      /* the alpha is ignored */
}

/* ---- Splash logo ---- */

#define TEST_LOGO_PATH      "dmst7789_test_logo.dmvir"
#define TEST_WIDTH          16
#define TEST_HEIGHT         8
#define TEST_BLUE           0x0000FFU

static void put_le16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put_le32(uint8_t *p, uint32_t v) { put_le16(p, (uint16_t)v); put_le16(p + 2, (uint16_t)(v >> 16)); }

/* A 4x2 RGB565A8 .dmvir: row 0 red, green, blue (transparent), white (half transparent); row 1 red. `compression` makes it a packed
 * .dmvi instead. */
static bool write_test_logo(const char *compression)
{
    uint8_t file[56 + 4 * 2 * 2 + 4 * 2];
    const uint16_t colors[8] = { 0xF800, 0x07E0, 0x001F, 0xFFFF, 0xF800, 0xF800, 0xF800, 0xF800 };
    const uint8_t alpha[8] = { 255, 255, 0, 128, 255, 255, 255, 255 };

    memset(file, 0, sizeof(file));
    memcpy(file, "DMVI", 4);
    put_le16(&file[6], 1);
    put_le32(&file[8], sizeof(file));
    put_le16(&file[12], 4);
    put_le16(&file[14], 2);
    file[16] = 3;                                   /* RGB565A8 */
    put_le32(&file[20], 4 * 2);                     /* stride */
    put_le32(&file[24], 56);                        /* pixels */
    put_le32(&file[28], 4);                         /* alpha_stride */
    put_le32(&file[32], 56 + 16);                   /* alpha */
    strcpy((char *)&file[40], compression);
    put_le32(&file[52], sizeof(file) - 56);
    for (int i = 0; i < 8; i++)
    {
        put_le16(&file[56 + 2 * i], colors[i]);
        file[56 + 16 + i] = alpha[i];
    }
    void *fp = Dmod_FileOpen(TEST_LOGO_PATH, "wb");
    if (fp == NULL)
        return false;
    bool written = Dmod_FileWrite(file, 1, sizeof(file), fp) == sizeof(file);
    Dmod_FileClose(fp);
    return written;
}

/* The 16x8 framebuffer filled with the background (blue, little-endian RGB565) */
static void fill_screen(uint16_t *fb)
{
    for (int i = 0; i < TEST_WIDTH * TEST_HEIGHT; i++)
        fb[i] = 0x001F;
}

static dmst7789_splash_target_t target(uint16_t *fb, uint16_t width, uint16_t height)
{
    dmst7789_splash_target_t t = { (uint8_t *)fb, (uint32_t)width * 2U, width, height, TEST_BLUE };
    return t;
}

DMOD_TEST_STEP(splash_logo_is_drawn_centered)
{
    uint16_t fb[TEST_WIDTH * TEST_HEIGHT];
    fill_screen(fb);
    DMOD_TEST_EXPECT_TRUE(write_test_logo(""));
    dmst7789_splash_target_t t = target(fb, TEST_WIDTH, TEST_HEIGHT);
    DMOD_TEST_EXPECT_EQ(dmst7789_splash_draw(TEST_LOGO_PATH, &t), 0);
    /* The 4x2 logo sits at x 6..9, y 3..4 */
    DMOD_TEST_EXPECT_EQ(fb[3 * TEST_WIDTH + 6], 0xF800);
    DMOD_TEST_EXPECT_EQ(fb[3 * TEST_WIDTH + 7], 0x07E0);
    DMOD_TEST_EXPECT_EQ(fb[3 * TEST_WIDTH + 8], 0x001F);    /* Transparent: the background */
    DMOD_TEST_EXPECT_EQ(fb[3 * TEST_WIDTH + 9], 0x841F);    /* White half over blue */
    DMOD_TEST_EXPECT_EQ(fb[4 * TEST_WIDTH + 9], 0xF800);
    DMOD_TEST_EXPECT_EQ(fb[2 * TEST_WIDTH + 6], 0x001F);    /* Around it: untouched */
    DMOD_TEST_EXPECT_EQ(fb[4 * TEST_WIDTH + 10], 0x001F);
}

DMOD_TEST_STEP(splash_logo_is_cropped_to_a_small_screen)
{
    uint16_t fb[2 * 2];
    for (int i = 0; i < 4; i++)
        fb[i] = 0x001F;
    DMOD_TEST_EXPECT_TRUE(write_test_logo(""));
    dmst7789_splash_target_t t = target(fb, 2, 2);
    DMOD_TEST_EXPECT_EQ(dmst7789_splash_draw(TEST_LOGO_PATH, &t), 0);
    /* The 4x2 logo centered on 2x2: its columns 1 and 2 (green, transparent) of row 0 and (red, red) of row 1 */
    DMOD_TEST_EXPECT_EQ(fb[0], 0x07E0);
    DMOD_TEST_EXPECT_EQ(fb[1], 0x001F);
    DMOD_TEST_EXPECT_EQ(fb[2], 0xF800);
    DMOD_TEST_EXPECT_EQ(fb[3], 0xF800);
}

DMOD_TEST_STEP(splash_logo_problems_are_reported)
{
    uint16_t fb[TEST_WIDTH * TEST_HEIGHT];
    fill_screen(fb);
    dmst7789_splash_target_t t = target(fb, TEST_WIDTH, TEST_HEIGHT);
    DMOD_TEST_EXPECT_EQ(dmst7789_splash_draw("no_such_logo.dmvir", &t), -2);        /* -ENOENT */
    DMOD_TEST_EXPECT_TRUE(write_test_logo("fastlz"));                                /* a packed .dmvi */
    DMOD_TEST_EXPECT_EQ(dmst7789_splash_draw(TEST_LOGO_PATH, &t), -95);             /* -ENOTSUP */
    DMOD_TEST_EXPECT_EQ(dmst7789_splash_draw(NULL, &t), -22);                        /* -EINVAL */
    DMOD_TEST_EXPECT_EQ(dmst7789_splash_draw(TEST_LOGO_PATH, NULL), -22);
    DMOD_TEST_EXPECT_EQ(fb[3 * TEST_WIDTH + 6], 0x001F);                            /* the screen stays as it was */
}
