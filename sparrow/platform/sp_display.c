#include "sp_display.h"

#include <time.h>

#include "sp_display_backends.h"

uint32_t sp_tick_ms(void)
{
    struct timespec now;

    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint32_t)((uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u);
}

SpStatus sp_display_init(const SpDisplayConfig *config)
{
    lv_init();
    lv_tick_set_cb(sp_tick_ms);

    switch (config->kind) {
    case SP_DISPLAY_SDL:
        return sp_display_sdl_init(config);
    case SP_DISPLAY_HEADLESS:
        return sp_display_headless_init(config);
    case SP_DISPLAY_FBDEV:
        return sp_display_fbdev_init(config);
    }
    return SP_ERR_ARGUMENT;
}
