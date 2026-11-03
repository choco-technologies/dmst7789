#define DMOD_ENABLE_REGISTRATION    ON
#include "dmod.h"
#include "dmst7789.h"
#include "dmdrvi.h"
#include "dmini.h"
#include "dmosi.h"
#include "dmgpio_types.h"
#include <errno.h>
#include <string.h>

/*
 * dmst7789: a display with an ST7789 controller on SPI (240x320 at most, RGB565).
 *
 * The controller keeps the picture in a RAM of its own, so there is no scan-out: the driver keeps a framebuffer (RGB565 in memory, the
 * heap puts it in the external RAM where there is one) that clients draw into, and PRESENT sends the area drawn to the panel. The
 * node follows the generic graphics interface (DMDRVI_IOCTL_GFX_*) of dmdrvi, as dmlcdtft does.
 *
 * The wiring is done by dmdevfs: the sections of the display, of its SPI bus (dmspi), of the data/command line, of the reset and of
 * the backlight are in one friends_group, and the roles of the friends (friend_role=...) tell this driver which node is which:
 *
 *   spi        the dmspi node the display is on (required)
 *   dc         the data/command line: low for a command, high for data (required)
 *   reset      the reset line, low = reset (when the configuration says reset=yes)
 *   backlight  the backlight enable (optional)
 *
 * The chip select is the business of dmspi (friend_role=chip_select, nss_mode=soft): it frames every write. The panel is started once
 * the required friends are there, whichever order they come in.
 */

/* Magic set to "ST78" */
#define DMST7789_CONTEXT_MAGIC      0x53543738

#define PANEL_MAX_WIDTH             240U
#define PANEL_MAX_HEIGHT            320U
#define BOUNCE_MAX_BYTES            4092U       /* one descriptor of the DMA of dmspi's port, the bounce buffer is in the internal RAM */

#define CMD_SLPOUT                  0x11
#define CMD_INVOFF                  0x20
#define CMD_INVON                   0x21
#define CMD_DISPOFF                 0x28
#define CMD_DISPON                  0x29
#define CMD_CASET                   0x2A
#define CMD_RASET                   0x2B
#define CMD_RAMWR                   0x2C
#define CMD_MADCTL                  0x36
#define CMD_COLMOD                  0x3A

typedef struct
{
    char   *path;                   /**< Node of the friend (NULL: not there) */
    void   *file;                   /**< Kept open once used */
} node_t;

typedef struct
{
    uint16_t x0, y0, x1, y1;        /**< Inclusive; x0 > x1: nothing */
} area_t;

struct dmdrvi_context
{
    uint32_t            magic;
    dmst7789_config_t   config;
    uint8_t             instance;
    uint16_t            width;              /**< As shown (after the rotation) */
    uint16_t            height;
    uint32_t            stride;
    uint32_t            framebuffer_size;
    uint8_t            *framebuffer;
    uint8_t            *bounce;             /**< Swapped lines on their way to the SPI */
    uint32_t            bounce_lines;
    dmosi_mutex_t       lock;
    node_t              spi;
    node_t              dc;
    node_t              reset;
    node_t              backlight;
    bool                reset_wired;        /**< The configuration says there is a reset line to wait for */
    bool                backlight_active_high;
    bool                started;            /**< The panel was initialised */
    bool                display_enabled;
    bool                backlight_on;
    area_t              dirty;              /**< Written through write() / FILL_RECT, not presented yet */
};

static bool is_valid_context(dmdrvi_context_t context)
{
    return context != NULL && context->magic == DMST7789_CONTEXT_MAGIC;
}

/* ---- API ---- */

dmod_dmst7789_api_declaration(1.0, bool, _validate_config, ( const dmst7789_config_t *config ))
{
    if (config == NULL || config->width == 0 || config->height == 0 || config->rotation > dmst7789_rotation_270)
        return false;
    /* The RAM of the controller is 240 x 320; the panel is a window of it */
    return (uint32_t)config->width + config->offset_x <= PANEL_MAX_WIDTH &&
           (uint32_t)config->height + config->offset_y <= PANEL_MAX_HEIGHT;
}

dmod_dmst7789_api_declaration(1.0, uint16_t, _color_to_rgb565, ( uint32_t argb ))
{
    uint32_t r = (argb >> 16) & 0xFFU, g = (argb >> 8) & 0xFFU, b = argb & 0xFFU;
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

/* ---- Configuration ---- */

static bool string_to_switch(const char *text, bool fallback)
{
    if (text == NULL)
        return fallback;
    if (strcmp(text, "on") == 0 || strcmp(text, "yes") == 0 || strcmp(text, "true") == 0 || strcmp(text, "1") == 0)
        return true;
    if (strcmp(text, "off") == 0 || strcmp(text, "no") == 0 || strcmp(text, "false") == 0 || strcmp(text, "0") == 0)
        return false;
    return fallback;
}

/* A number, decimal or 0x hex */
static bool parse_number(const char *text, uint32_t *value)
{
    uint64_t n = 0;
    unsigned base = 10;
    if (text == NULL || *text == '\0')
        return false;
    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
    {
        base = 16;
        text += 2;
    }
    if (*text == '\0')
        return false;
    for (; *text != '\0'; text++)
    {
        int d = (*text >= '0' && *text <= '9') ? *text - '0'
              : (*text >= 'a' && *text <= 'f') ? *text - 'a' + 10
              : (*text >= 'A' && *text <= 'F') ? *text - 'A' + 10 : -1;
        if (d < 0 || (unsigned)d >= base)
            return false;
        n = n * base + (unsigned)d;
        if (n > 0xFFFFFFFFULL)
            return false;
    }
    *value = (uint32_t)n;
    return true;
}

static int read_config(dmdrvi_context_t context, dmini_context_t ini)
{
    dmst7789_config_t *c = &context->config;
    int rotation = dmini_get_int(ini, NULL, "rotation", 0);
    const char *order = dmini_get_string(ini, NULL, "rgb_order", "rgb");
    const char *clear = dmini_get_string(ini, NULL, "clear_color", NULL);

    context->instance = (uint8_t)dmini_get_int(ini, NULL, "instance", 0);
    c->width    = (uint16_t)dmini_get_int(ini, NULL, "width", PANEL_MAX_WIDTH);
    c->height   = (uint16_t)dmini_get_int(ini, NULL, "height", PANEL_MAX_HEIGHT);
    c->offset_x = (uint16_t)dmini_get_int(ini, NULL, "offset_x", 0);
    c->offset_y = (uint16_t)dmini_get_int(ini, NULL, "offset_y", 0);
    c->invert   = string_to_switch(dmini_get_string(ini, NULL, "invert", NULL), true);
    c->clear_color = 0xFF000000U;
    if (rotation != 0 && rotation != 90 && rotation != 180 && rotation != 270)
    {
        DMOD_LOG_ERROR("Invalid rotation %d (0, 90, 180 or 270)\n", rotation);
        return -EINVAL;
    }
    c->rotation = (dmst7789_rotation_t)(rotation / 90);
    if (strcmp(order, "bgr") == 0)
        c->bgr = true;
    else if (strcmp(order, "rgb") != 0)
    {
        DMOD_LOG_ERROR("Invalid rgb_order '%s' (rgb or bgr)\n", order);
        return -EINVAL;
    }
    if (clear != NULL && !parse_number(clear, &c->clear_color))
    {
        DMOD_LOG_ERROR("Invalid clear_color '%s'\n", clear);
        return -EINVAL;
    }
    context->reset_wired = string_to_switch(dmini_get_string(ini, NULL, "reset", NULL), false);
    context->backlight_active_high = strcmp(dmini_get_string(ini, NULL, "backlight_active_level", "high"), "low") != 0;
    context->backlight_on = string_to_switch(dmini_get_string(ini, NULL, "backlight", NULL), true);
    context->display_enabled = string_to_switch(dmini_get_string(ini, NULL, "display", NULL), true);

    if (!dmst7789_validate_config(c))
    {
        DMOD_LOG_ERROR("Invalid display configuration (%ux%u, offsets %u/%u)\n", c->width, c->height, c->offset_x, c->offset_y);
        return -EINVAL;
    }
    bool swap = c->rotation == dmst7789_rotation_90 || c->rotation == dmst7789_rotation_270;
    context->width  = swap ? c->height : c->width;
    context->height = swap ? c->width : c->height;
    context->stride = (uint32_t)context->width * 2U;
    context->framebuffer_size = context->stride * context->height;
    return 0;
}

/* ---- The nodes of the friends ---- */

static void node_close(node_t *node)
{
    if (node->file != NULL)
        Dmod_FileClose(node->file);
    node->file = NULL;
}

static void node_forget(node_t *node)
{
    node_close(node);
    Dmod_Free(node->path);
    node->path = NULL;
}

static void *node_file(node_t *node)
{
    if (node->file == NULL && node->path != NULL)
        node->file = Dmod_FileOpen(node->path, "r+");
    return node->file;
}

static int gpio_set(node_t *node, bool high)
{
    dmgpio_pins_state_t state = high ? dmgpio_pins_state_all_high : dmgpio_pins_state_all_low;
    void *file = node_file(node);
    if (file == NULL)
        return -ENODEV;
    if (Dmod_Ioctl(file, dmgpio_ioctl_cmd_set_pins_state, &state) != 0)
    {
        node_close(node);
        return -EIO;
    }
    return 0;
}

static int spi_write(dmdrvi_context_t context, const void *data, size_t size)
{
    void *file = node_file(&context->spi);
    if (file == NULL)
        return -ENODEV;
    if (Dmod_FileWrite(data, 1, size, file) != size)
    {
        node_close(&context->spi);
        return -EIO;
    }
    return 0;
}

/* ---- The controller ---- */

static int send_command(dmdrvi_context_t context, uint8_t command)
{
    int ret = gpio_set(&context->dc, false);
    return ret != 0 ? ret : spi_write(context, &command, 1);
}

static int send_data(dmdrvi_context_t context, const void *data, size_t size)
{
    int ret = gpio_set(&context->dc, true);
    return ret != 0 ? ret : spi_write(context, data, size);
}

static int send_command_data(dmdrvi_context_t context, uint8_t command, const uint8_t *data, size_t size)
{
    int ret = send_command(context, command);
    return (ret != 0 || size == 0) ? ret : send_data(context, data, size);
}

#define COMMAND(context, command, ...)                                                          \
    do {                                                                                        \
        static const uint8_t data_[] = { __VA_ARGS__ };                                         \
        if (send_command_data((context), (command), data_, sizeof(data_)) != 0)                 \
            return -EIO;                                                                        \
    } while (0)

/* MADCTL: MY 0x80, MX 0x40, MV 0x20, BGR 0x08 */
static uint8_t madctl(const dmst7789_config_t *c)
{
    static const uint8_t rotation[] = { 0x00, 0x60, 0xC0, 0xA0 };
    return (uint8_t)(rotation[c->rotation] | (c->bgr ? 0x08U : 0x00U));
}

/* The start sequence of the vendor of the panel (Waveshare ESP32-S3-Touch-LCD-2.8), 16 bits per pixel */
static int panel_init(dmdrvi_context_t context)
{
    const dmst7789_config_t *c = &context->config;
    uint8_t mad = madctl(c);

    if (context->reset.path != NULL)
    {
        (void)gpio_set(&context->reset, false);
        dmosi_thread_sleep(50);
        (void)gpio_set(&context->reset, true);
        dmosi_thread_sleep(120);
    }
    if (send_command(context, CMD_SLPOUT) != 0)
        return -EIO;
    dmosi_thread_sleep(120);
    if (send_command_data(context, CMD_MADCTL, &mad, 1) != 0)
        return -EIO;
    COMMAND(context, CMD_COLMOD, 0x05);
    COMMAND(context, 0xB0, 0x00, 0xE8);
    COMMAND(context, 0xB2, 0x0C, 0x0C, 0x00, 0x33, 0x33);
    COMMAND(context, 0xB7, 0x75);
    COMMAND(context, 0xBB, 0x1A);
    COMMAND(context, 0xC0, 0x2C);
    COMMAND(context, 0xC2, 0x01, 0xFF);
    COMMAND(context, 0xC3, 0x13);
    COMMAND(context, 0xC4, 0x20);
    COMMAND(context, 0xC6, 0x0F);
    COMMAND(context, 0xD0, 0xA4, 0xA1);
    COMMAND(context, 0xD6, 0xA1);
    COMMAND(context, 0xE0, 0xD0, 0x0D, 0x14, 0x0D, 0x0D, 0x09, 0x38, 0x44, 0x4E, 0x3A, 0x17, 0x18, 0x2F, 0x30);
    COMMAND(context, 0xE1, 0xD0, 0x09, 0x0F, 0x08, 0x07, 0x14, 0x37, 0x44, 0x4D, 0x38, 0x15, 0x16, 0x2C, 0x2E);
    if (send_command(context, c->invert ? CMD_INVON : CMD_INVOFF) != 0)
        return -EIO;
    dmosi_thread_sleep(10);
    return send_command(context, context->display_enabled ? CMD_DISPON : CMD_DISPOFF);
}

static void backlight_apply(dmdrvi_context_t context)
{
    if (context->backlight.path != NULL)
    {
        bool on = context->display_enabled && context->backlight_on;
        (void)gpio_set(&context->backlight, on == context->backlight_active_high);
    }
}

/* ---- The framebuffer ---- */

static void dirty_add(dmdrvi_context_t context, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    area_t *d = &context->dirty;
    if (d->x0 > d->x1)
    {
        d->x0 = x0; d->y0 = y0; d->x1 = x1; d->y1 = y1;
        return;
    }
    d->x0 = x0 < d->x0 ? x0 : d->x0;
    d->y0 = y0 < d->y0 ? y0 : d->y0;
    d->x1 = x1 > d->x1 ? x1 : d->x1;
    d->y1 = y1 > d->y1 ? y1 : d->y1;
}

static void dirty_clear(dmdrvi_context_t context)
{
    context->dirty = (area_t){ 1, 1, 0, 0 };
}

static void fill_rect(dmdrvi_context_t context, const dmdrvi_gfx_fill_rect_t *rect)
{
    if (rect->x >= context->width || rect->y >= context->height || rect->width == 0 || rect->height == 0)
        return;
    uint32_t w = rect->width > context->width - rect->x ? context->width - rect->x : rect->width;
    uint32_t h = rect->height > context->height - rect->y ? context->height - rect->y : rect->height;
    uint16_t pixel = dmst7789_color_to_rgb565(rect->color);

    uint16_t *first = (uint16_t *)(context->framebuffer + (uint32_t)rect->y * context->stride + (uint32_t)rect->x * 2U);
    for (uint32_t i = 0; i < w; i++)
        first[i] = pixel;
    for (uint32_t line = 1; line < h; line++)
        memcpy(context->framebuffer + ((uint32_t)rect->y + line) * context->stride + (uint32_t)rect->x * 2U, first, w * 2U);
    dirty_add(context, rect->x, rect->y, (uint16_t)(rect->x + w - 1U), (uint16_t)(rect->y + h - 1U));
}

/* Sends [x0, x1] x [y0, y1] of the framebuffer to the panel; the caller holds the lock */
static int push_area(dmdrvi_context_t context, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1)
{
    const dmst7789_config_t *c = &context->config;
    uint32_t w = x1 - x0 + 1U;
    uint32_t cx0 = x0 + c->offset_x, cx1 = x1 + c->offset_x;
    uint32_t cy0 = y0 + c->offset_y, cy1 = y1 + c->offset_y;
    uint8_t caset[4] = { (uint8_t)(cx0 >> 8), (uint8_t)cx0, (uint8_t)(cx1 >> 8), (uint8_t)cx1 };
    uint8_t raset[4] = { (uint8_t)(cy0 >> 8), (uint8_t)cy0, (uint8_t)(cy1 >> 8), (uint8_t)cy1 };

    if (!context->started)
        return -EAGAIN;
    if (send_command_data(context, CMD_CASET, caset, 4) != 0 || send_command_data(context, CMD_RASET, raset, 4) != 0 ||
        send_command(context, CMD_RAMWR) != 0)
        return -EIO;

    /* The pixels go out most significant byte first; the framebuffer holds them little-endian */
    uint32_t lines_per_chunk = context->bounce_lines;
    for (uint32_t y = y0; y <= y1;)
    {
        uint32_t lines = y1 - y + 1U < lines_per_chunk ? y1 - y + 1U : lines_per_chunk;
        uint8_t *out = context->bounce;
        for (uint32_t line = 0; line < lines; line++)
        {
            const uint8_t *in = context->framebuffer + (y + line) * context->stride + x0 * 2U;
            for (uint32_t i = 0; i < w; i++)
            {
                *out++ = in[2U * i + 1U];
                *out++ = in[2U * i];
            }
        }
        if (send_data(context, context->bounce, (size_t)lines * w * 2U) != 0)
            return -EIO;
        y += lines;
    }
    return 0;
}

static int present(dmdrvi_context_t context, const dmdrvi_gfx_rect_t *area)
{
    uint32_t x0 = 0, y0 = 0, x1 = context->width - 1U, y1 = context->height - 1U;
    if (area != NULL)
    {
        if (area->x >= context->width || area->y >= context->height || area->width == 0 || area->height == 0)
            return 0;
        x0 = area->x;
        y0 = area->y;
        x1 = x0 + area->width - 1U > x1 ? x1 : x0 + area->width - 1U;
        y1 = y0 + area->height - 1U > y1 ? y1 : y0 + area->height - 1U;
    }
    dmosi_mutex_lock(context->lock);
    int ret = push_area(context, x0, y0, x1, y1);
    if (ret == 0)
        dirty_clear(context);
    dmosi_mutex_unlock(context->lock);
    return ret;
}

static int set_display_enabled(dmdrvi_context_t context, bool enabled)
{
    dmosi_mutex_lock(context->lock);
    int ret = context->started ? send_command(context, enabled ? CMD_DISPON : CMD_DISPOFF) : 0;
    if (ret == 0)
    {
        context->display_enabled = enabled;
        backlight_apply(context);
    }
    dmosi_mutex_unlock(context->lock);
    return ret;
}

/* Starts the panel once the required friends are there (whichever order they come in) and shows the framebuffer */
static void try_start(dmdrvi_context_t context)
{
    if (context->started || context->spi.path == NULL || context->dc.path == NULL || (context->reset_wired && context->reset.path == NULL))
        return;
    dmosi_mutex_lock(context->lock);
    if (panel_init(context) == 0)
    {
        context->started = true;
        if (push_area(context, 0, 0, context->width - 1U, context->height - 1U) == 0)
            dirty_clear(context);
        backlight_apply(context);
        DMOD_LOG_INFO("ST7789 %ux%u started (rotation %u)\n", context->width, context->height, (unsigned)context->config.rotation * 90U);
    }
    else
    {
        DMOD_LOG_ERROR("ST7789: the start sequence failed (SPI / D/C nodes: %s / %s)\n", context->spi.path, context->dc.path);
    }
    dmosi_mutex_unlock(context->lock);
}

/* ---- DMOD lifecycle ---- */

int dmod_init(const Dmod_Config_t *Config)
{
    (void)Config;
    DMOD_LOG_INFO("DMST7789 module initialized\n");
    return 0;
}

int dmod_deinit(void)
{
    DMOD_LOG_INFO("DMST7789 module deinitialized\n");
    return 0;
}

/* ---- DMDRVI interface ---- */

static void *alloc_internal(size_t size)
{
    void *p = Dmod_MallocTagged("dma", size);          /* internal RAM: dmspi's DMA moves it in place */
    return p != NULL ? p : Dmod_Malloc(size);
}

dmod_dmdrvi_dif_api_declaration(2.0, dmst7789, dmdrvi_context_t, _create, ( dmini_context_t config, dmdrvi_dev_num_t* dev_num ))
{
    if (config == NULL || dev_num == NULL)
        return NULL;
    dmdrvi_context_t context = Dmod_Malloc(sizeof(struct dmdrvi_context));
    if (context == NULL)
        return NULL;
    memset(context, 0, sizeof(*context));
    context->magic = DMST7789_CONTEXT_MAGIC;
    dirty_clear(context);

    if (read_config(context, config) != 0)
        goto fail;
    context->lock = dmosi_mutex_create(false);
    context->framebuffer = Dmod_AlignedMalloc(context->framebuffer_size, 32);
    uint32_t line_bytes = context->stride;
    context->bounce_lines = BOUNCE_MAX_BYTES / line_bytes;
    if (context->bounce_lines == 0U)
        context->bounce_lines = 1U;
    if (context->bounce_lines > context->height)
        context->bounce_lines = context->height;
    context->bounce = alloc_internal((size_t)context->bounce_lines * line_bytes);
    if (context->lock == NULL || context->framebuffer == NULL || context->bounce == NULL)
    {
        DMOD_LOG_ERROR("ST7789: out of memory (framebuffer %u bytes)\n", (unsigned)context->framebuffer_size);
        goto fail;
    }
    /* The picture starts as clear_color: the panel never shows the garbage of the heap */
    dmdrvi_gfx_fill_rect_t all = { 0, 0, context->width, context->height, context->config.clear_color };
    fill_rect(context, &all);

    dev_num->flags = DMDRVI_NUM_MAJOR;
    dev_num->major = (dmdrvi_dev_id_t)context->instance;
    return context;

fail:
    if (context->lock != NULL)
        dmosi_mutex_destroy(context->lock);
    Dmod_Free(context->framebuffer);
    Dmod_Free(context->bounce);
    context->magic = 0;
    Dmod_Free(context);
    return NULL;
}

dmod_dmdrvi_dif_api_declaration(2.0, dmst7789, void, _free, ( dmdrvi_context_t context ))
{
    if (!is_valid_context(context))
        return;
    /* The nodes of the friends are not touched: dmdevfs tears them down before this call, in reverse order of the sections */
    Dmod_Free(context->spi.path);
    Dmod_Free(context->dc.path);
    Dmod_Free(context->reset.path);
    Dmod_Free(context->backlight.path);
    dmosi_mutex_destroy(context->lock);
    Dmod_Free(context->framebuffer);
    Dmod_Free(context->bounce);
    context->magic = 0;
    Dmod_Free(context);
}

dmod_dmdrvi_dif_api_declaration(2.0, dmst7789, void, _friend_changed, ( dmdrvi_context_t context, const dmdrvi_friend_info_t* info ))
{
    if (!is_valid_context(context) || info == NULL || info->friend_role == NULL)
        return;
    node_t *node = strcmp(info->friend_role, "spi") == 0 ? &context->spi
                 : strcmp(info->friend_role, "dc") == 0 ? &context->dc
                 : strcmp(info->friend_role, "reset") == 0 ? &context->reset
                 : strcmp(info->friend_role, "backlight") == 0 ? &context->backlight : NULL;
    if (node == NULL)
        return;                                         /* chip_select is dmspi's */
    node_forget(node);
    if (info->state != dmdrvi_dev_state_ready || info->node_path == NULL)
        return;
    node->path = Dmod_StrDup(info->node_path);
    if (node->path == NULL)
    {
        DMOD_LOG_ERROR("ST7789: out of memory for the path of a friend\n");
        return;
    }
    if (node == &context->backlight)
        backlight_apply(context);
    try_start(context);
}

dmod_dmdrvi_dif_api_declaration(2.0, dmst7789, void*, _open, ( dmdrvi_context_t context, int flags, const dmdrvi_dev_num_t* dev_num ))
{
    return is_valid_context(context) ? context : NULL;
}

dmod_dmdrvi_dif_api_declaration(2.0, dmst7789, void, _close, ( dmdrvi_context_t context, void* handle ))
{
}

dmod_dmdrvi_dif_api_declaration(2.0, dmst7789, dmdrvi_ssize_t, _read, ( dmdrvi_context_t context, void* handle, void* buffer, size_t size, dmdrvi_offset_t offset ))
{
    if (!is_valid_context(context) || (buffer == NULL && size != 0) || offset < 0)
        return -EINVAL;
    if (size == 0 || offset >= (dmdrvi_offset_t)context->framebuffer_size)
        return 0;
    size_t available = context->framebuffer_size - (size_t)offset;
    size_t count = size < available ? size : available;
    memcpy(buffer, context->framebuffer + offset, count);
    return (dmdrvi_ssize_t)count;
}

/* Writes into the framebuffer; what is written reaches the panel at the next PRESENT / flush */
dmod_dmdrvi_dif_api_declaration(2.0, dmst7789, dmdrvi_ssize_t, _write, ( dmdrvi_context_t context, void* handle, const void* buffer, size_t size, dmdrvi_offset_t offset ))
{
    if (!is_valid_context(context) || (buffer == NULL && size != 0) || offset < 0)
        return -EINVAL;
    if (size == 0)
        return 0;
    if (offset >= (dmdrvi_offset_t)context->framebuffer_size)
        return -ENOSPC;
    size_t available = context->framebuffer_size - (size_t)offset;
    size_t count = size < available ? size : available;
    memcpy(context->framebuffer + offset, buffer, count);
    uint32_t first_line = (uint32_t)offset / context->stride;
    uint32_t last_line = ((uint32_t)offset + (uint32_t)count - 1U) / context->stride;
    dirty_add(context, 0, (uint16_t)first_line, (uint16_t)(context->width - 1U), (uint16_t)last_line);
    return (dmdrvi_ssize_t)count;
}

static bool ioctl_needs_arg(int command)
{
    return command == (int)DMDRVI_IOCTL_GFX_GET_INFO || command == (int)DMDRVI_IOCTL_GFX_GET_FRAMEBUFFER ||
           command == (int)DMDRVI_IOCTL_GFX_FILL_RECT || command == (int)DMDRVI_IOCTL_GFX_SET_DISPLAY_ENABLED ||
           command == (int)DMDRVI_IOCTL_GFX_GET_DISPLAY_ENABLED || command == (int)DMDRVI_IOCTL_GFX_SET_BACKLIGHT ||
           command == (int)DMDRVI_IOCTL_GFX_GET_BACKLIGHT || command == (int)dmst7789_ioctl_cmd_get_config;
}

dmod_dmdrvi_dif_api_declaration(2.0, dmst7789, int, _ioctl, ( dmdrvi_context_t context, void* handle, int command, void* arg ))
{
    if (!is_valid_context(context))
        return -EINVAL;
    if (arg == NULL && ioctl_needs_arg(command))
        return -EINVAL;

    switch (command)
    {
        case DMDRVI_IOCTL_GFX_GET_INFO:
        {
            dmdrvi_gfx_info_t *info = (dmdrvi_gfx_info_t *)arg;
            info->width = context->width;
            info->height = context->height;
            info->pixel_format = DMDRVI_GFX_PIXEL_FORMAT_RGB565;
            info->bytes_per_pixel = 2;
            info->stride = context->stride;
            info->framebuffer_size = context->framebuffer_size;
            info->buffer_count = 1;
            return 0;
        }
        case DMDRVI_IOCTL_GFX_GET_FRAMEBUFFER:
            *(void **)arg = context->framebuffer;
            return 0;
        case DMDRVI_IOCTL_GFX_FILL_RECT:
            fill_rect(context, (const dmdrvi_gfx_fill_rect_t *)arg);
            return 0;
        case DMDRVI_IOCTL_GFX_PRESENT:
            return present(context, (const dmdrvi_gfx_rect_t *)arg);
        case DMDRVI_IOCTL_GFX_SET_DISPLAY_ENABLED:
            return set_display_enabled(context, *(const bool *)arg);
        case DMDRVI_IOCTL_GFX_GET_DISPLAY_ENABLED:
            *(bool *)arg = context->display_enabled;
            return 0;
        case DMDRVI_IOCTL_GFX_SET_BACKLIGHT:
            context->backlight_on = *(const bool *)arg;
            backlight_apply(context);
            return 0;
        case DMDRVI_IOCTL_GFX_GET_BACKLIGHT:
            *(bool *)arg = context->backlight_on;
            return 0;
        case DMDRVI_IOCTL_GFX_SWAP_BUFFERS:
        case DMDRVI_IOCTL_GFX_WAIT_VSYNC:
            return -ENOTSUP;                            /* one buffer, no scan-out to wait for */
        case dmst7789_ioctl_cmd_get_config:
            *(dmst7789_config_t *)arg = context->config;
            return 0;
        default:
            return -ENOTTY;
    }
}

/* flush(): what was written through write() / FILL_RECT since the last PRESENT goes to the panel */
dmod_dmdrvi_dif_api_declaration(2.0, dmst7789, int, _flush, ( dmdrvi_context_t context, void* handle ))
{
    if (!is_valid_context(context))
        return -EINVAL;
    if (context->dirty.x0 > context->dirty.x1)
        return 0;
    dmdrvi_gfx_rect_t area = { context->dirty.x0, context->dirty.y0, (uint16_t)(context->dirty.x1 - context->dirty.x0 + 1U),
                               (uint16_t)(context->dirty.y1 - context->dirty.y0 + 1U) };
    return present(context, &area);
}

dmod_dmdrvi_dif_api_declaration(2.0, dmst7789, int, _stat, ( dmdrvi_context_t context, const char* path, dmdrvi_stat_t* stat ))
{
    if (!is_valid_context(context) || stat == NULL)
        return -EINVAL;
    stat->size = (dmdrvi_size_t)context->framebuffer_size;
    stat->mode = 0666;
    return 0;
}
