#include <stdlib.h>

#include "sp_display_backends.h"

#define BUFFER_LINES 60

static void discard_flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels)
{
    (void)area;
    (void)pixels;
    lv_display_flush_ready(display);
}

SpStatus sp_display_headless_init(const SpDisplayConfig *config)
{
    lv_display_t *display = lv_display_create(config->width, config->height);
    const size_t buffer_size = (size_t)config->width * BUFFER_LINES * 4;
    void *buffer = malloc(buffer_size);

    if (display == NULL || buffer == NULL) {
        free(buffer);
        return SP_ERR_IO;
    }
    lv_display_set_color_format(display, LV_COLOR_FORMAT_ARGB8888);
    lv_display_set_buffers(display, buffer, NULL, (uint32_t)buffer_size,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, discard_flush);
    return SP_OK;
}
