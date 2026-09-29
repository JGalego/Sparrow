#ifndef SPARROW_HMI_PILL_H
#define SPARROW_HMI_PILL_H

#include "lvgl.h"
#include "sp_severity.h"

/* A rounded status label with a leading dot, tinted by severity. */
lv_obj_t *sp_pill_create(lv_obj_t *parent, const lv_font_t *font, int32_t x, int32_t y,
                         int32_t width, int32_t height);

void sp_pill_set(lv_obj_t *pill, const char *text, SpSeverity severity);

#endif
