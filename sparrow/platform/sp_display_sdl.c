#include "sp_display_backends.h"

#ifdef SPARROW_WITH_SDL

SpStatus sp_display_sdl_init(const SpDisplayConfig *config)
{
    lv_display_t *display = lv_sdl_window_create(config->width, config->height);

    if (display == NULL) {
        return SP_ERR_IO;
    }
    lv_sdl_window_set_title(display, config->title != NULL ? config->title : "Sparrow");
    lv_sdl_mouse_create();
    lv_sdl_keyboard_create();
    return SP_OK;
}

#else

SpStatus sp_display_sdl_init(const SpDisplayConfig *config)
{
    (void)config;
    return SP_ERR_ARGUMENT;
}

#endif
