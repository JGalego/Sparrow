#ifndef SPARROW_PLATFORM_DISPLAY_BACKENDS_H
#define SPARROW_PLATFORM_DISPLAY_BACKENDS_H

#include "sp_display.h"

SpStatus sp_display_sdl_init(const SpDisplayConfig *config);
SpStatus sp_display_headless_init(const SpDisplayConfig *config);
SpStatus sp_display_fbdev_init(const SpDisplayConfig *config);

#endif
