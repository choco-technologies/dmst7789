#define DMOD_ENABLE_REGISTRATION ON
#include "dmod_test.h"
#include "dmst7789.h"

static dmst7789_t g_handle = NULL;

void dmod_test_setup(void)
{
    g_handle = dmst7789_create();
}

void dmod_test_teardown(void)
{
    dmst7789_destroy(g_handle);
    g_handle = NULL;
}

DMOD_TEST_STEP(dmst7789_create)
{
    DMOD_TEST_EXPECT_NOT_NULL(g_handle);
}

DMOD_TEST_STEP(dmst7789_is_valid)
{
    DMOD_TEST_EXPECT_TRUE(dmst7789_is_valid(g_handle));
}

DMOD_TEST_STEP(dmst7789_destroy_null)
{
    /* Destroying NULL must not crash. */
    dmst7789_destroy(NULL);
}
