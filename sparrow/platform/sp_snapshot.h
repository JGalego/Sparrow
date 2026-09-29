#ifndef SPARROW_PLATFORM_SNAPSHOT_H
#define SPARROW_PLATFORM_SNAPSHOT_H

#include "lvgl.h"
#include "sp_status.h"

/* Renders obj (and its children) off-screen and writes a binary PPM (P6) file. */
SpStatus sp_snapshot_write_ppm(lv_obj_t *obj, const char *path);

#endif
