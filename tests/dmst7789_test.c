#define DMOD_ENABLE_REGISTRATION ON
#include "dmod_test.h"
#include "dmst7789.h"

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
