#ifndef SPARROW_HMI_PANEL_H
#define SPARROW_HMI_PANEL_H

#include "lvgl.h"

#define SP_PANEL_HEADER_HEIGHT 34

/*
 * A titled card. Children are positioned relative to the panel; content
 * normally starts at y = SP_PANEL_HEADER_HEIGHT.
 */
lv_obj_t *sp_panel_create(lv_obj_t *parent, const char *title, int32_t x, int32_t y, int32_t width,
                          int32_t height);

#endif
