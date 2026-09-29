#ifndef SPARROW_PLATFORM_DISPLAY_H
#define SPARROW_PLATFORM_DISPLAY_H

#include <stdint.h>

#include "lvgl.h"
#include "sp_status.h"

typedef enum {
    SP_DISPLAY_SDL,      /* desktop window */
    SP_DISPLAY_HEADLESS, /* off-screen buffer, for tests and screenshots */
    SP_DISPLAY_FBDEV     /* Linux framebuffer with evdev pointer */
} SpDisplayKind;

typedef struct {
    SpDisplayKind kind;
    int32_t width;
    int32_t height;
    const char *title;        /* SDL window title */
    const char *fb_device;    /* fbdev: framebuffer node, default /dev/fb0 */
    const char *input_device; /* fbdev: evdev pointer node, default /dev/input/event0 */
} SpDisplayConfig;

/*
 * Creates the LVGL display and input devices for the requested backend and
 * installs the tick source. Returns SP_ERR_ARGUMENT if the backend was not
 * compiled in, SP_ERR_IO if it fails to start.
 */
SpStatus sp_display_init(const SpDisplayConfig *config);

/* Milliseconds from a monotonic clock. */
uint32_t sp_tick_ms(void);

#endif
