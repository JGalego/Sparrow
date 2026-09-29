#include "sp_display_backends.h"

#ifdef SPARROW_WITH_FBDEV

SpStatus sp_display_fbdev_init(const SpDisplayConfig *config)
{
    lv_display_t *display = lv_linux_fbdev_create();
    lv_indev_t *pointer;

    if (display == NULL) {
        return SP_ERR_IO;
    }
    lv_linux_fbdev_set_file(display, config->fb_device != NULL ? config->fb_device : "/dev/fb0");
    pointer =
        lv_evdev_create(LV_INDEV_TYPE_POINTER,
                        config->input_device != NULL ? config->input_device : "/dev/input/event0");
    return pointer != NULL ? SP_OK : SP_ERR_IO;
}

#else

SpStatus sp_display_fbdev_init(const SpDisplayConfig *config)
{
    (void)config;
    return SP_ERR_ARGUMENT;
}

#endif
