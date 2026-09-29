#include "sp_panel.h"

#include "sp_theme.h"

lv_obj_t *sp_panel_create(lv_obj_t *parent, const char *title, int32_t x, int32_t y, int32_t width,
                          int32_t height)
{
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_t *label;

    sp_theme_make_plain(panel);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_size(panel, width, height);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(panel, SP_COLOR_SURFACE, 0);
    lv_obj_set_style_radius(panel, SP_RADIUS, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_color(panel, SP_COLOR_BORDER, 0);

    label = sp_label_create(panel, SP_FONT_SMALL, SP_COLOR_TEXT_DIM);
    lv_obj_set_style_text_letter_space(label, 1, 0);
    lv_label_set_text(label, title);
    lv_obj_set_pos(label, 16, 12);
    return panel;
}
