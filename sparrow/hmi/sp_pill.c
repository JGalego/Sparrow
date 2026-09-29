#include "sp_pill.h"

#include "sp_theme.h"

/* The dot is child 0 and the label child 1. */
lv_obj_t *sp_pill_create(lv_obj_t *parent, const lv_font_t *font, int32_t x, int32_t y,
                         int32_t width, int32_t height)
{
    lv_obj_t *pill = lv_obj_create(parent);
    lv_obj_t *dot = lv_obj_create(pill);
    lv_obj_t *label = sp_label_create(pill, font, SP_COLOR_TEXT);

    sp_theme_make_plain(pill);
    lv_obj_set_pos(pill, x, y);
    lv_obj_set_size(pill, width, height);
    lv_obj_set_style_radius(pill, height / 2, 0);
    lv_obj_set_style_border_width(pill, 1, 0);
    lv_obj_set_style_bg_opa(pill, LV_OPA_20, 0);

    sp_theme_make_plain(dot);
    lv_obj_set_size(dot, 8, 8);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_align(dot, LV_ALIGN_LEFT_MID, height / 2 - 2, 0);

    lv_obj_align(label, LV_ALIGN_CENTER, 8, 0);
    return pill;
}

void sp_pill_set(lv_obj_t *pill, const char *text, SpSeverity severity)
{
    const lv_color_t color = sp_severity_color(severity);

    lv_obj_set_style_bg_color(pill, color, 0);
    lv_obj_set_style_border_color(pill, color, 0);
    lv_obj_set_style_bg_color(lv_obj_get_child(pill, 0), color, 0);
    lv_label_set_text(lv_obj_get_child(pill, 1), text);
}
