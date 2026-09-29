#ifndef SPARROW_HMI_THEME_H
#define SPARROW_HMI_THEME_H

#include <math.h>

#include "lvgl.h"
#include "sp_severity.h"

/*
 * Palette and type scale for all Sparrow widgets. Colors are used sparingly:
 * green, blue, amber and red carry status; everything else is a neutral.
 */
#define SP_COLOR_BACKGROUND     lv_color_hex(0x0D1218)
#define SP_COLOR_SURFACE        lv_color_hex(0x151C25)
#define SP_COLOR_SURFACE_RAISED lv_color_hex(0x1D2732)
#define SP_COLOR_BORDER         lv_color_hex(0x263240)
#define SP_COLOR_TRACK          lv_color_hex(0x283544)
#define SP_COLOR_TEXT           lv_color_hex(0xE7ECF1)
#define SP_COLOR_TEXT_DIM       lv_color_hex(0x8593A3)
#define SP_COLOR_ACCENT         lv_color_hex(0x3FB6C9)
#define SP_COLOR_BRAND          lv_color_hex(0xD93F45) /* logo mark only, never a status */

#define SP_FONT_SMALL   (&lv_font_montserrat_12)
#define SP_FONT_BODY    (&lv_font_montserrat_14)
#define SP_FONT_LABEL   (&lv_font_montserrat_16)
#define SP_FONT_TITLE   (&lv_font_montserrat_20)
#define SP_FONT_VALUE   (&lv_font_montserrat_28)
#define SP_FONT_DISPLAY (&lv_font_montserrat_40)

#define SP_RADIUS 10

/* LVGL draw descriptors take integer coordinates unless built with LV_USE_FLOAT. */
#define SP_PRECISE(value) ((lv_value_precise_t)lroundf(value))

lv_color_t sp_severity_color(SpSeverity severity);

/* Removes scrolling, padding, border and background so an object is a plain container. */
void sp_theme_make_plain(lv_obj_t *obj);

lv_obj_t *sp_label_create(lv_obj_t *parent, const lv_font_t *font, lv_color_t color);

#endif
