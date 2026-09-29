/*
 * LVGL configuration for Sparrow. Only options that differ from the LVGL
 * defaults are listed. Display and input drivers are selected by the compile
 * definitions SPARROW_WITH_SDL and SPARROW_WITH_FBDEV set in CMake.
 */
#if 1

#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 32

#define LV_USE_STDLIB_MALLOC  LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING  LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB

#define LV_DEF_REFR_PERIOD 16
#define LV_USE_OS          LV_OS_NONE

#define LV_DRAW_SW_COMPLEX 1
#define LV_USE_DRAW_SW     1

#define LV_USE_LOG    1
#define LV_LOG_LEVEL  LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 1

#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_40 1
#define LV_FONT_DEFAULT       (&lv_font_montserrat_14)

#define LV_USE_SNAPSHOT 1

#ifdef SPARROW_WITH_SDL
#define LV_USE_SDL          1
#define LV_SDL_INCLUDE_PATH <SDL2/SDL.h>
#define LV_SDL_RENDER_MODE  LV_DISPLAY_RENDER_MODE_PARTIAL
#define LV_SDL_BUF_COUNT    1
#define LV_SDL_ACCELERATED  0
#endif

#ifdef SPARROW_WITH_FBDEV
#define LV_USE_LINUX_FBDEV 1
#define LV_USE_EVDEV       1
#endif

#endif /* LV_CONF_H */
#endif
