#include "sp_button.h"

#include "sp_theme.h"

static void style_for_kind(lv_obj_t *button, SpButtonKind kind)
{
    lv_color_t accent = SP_COLOR_TEXT;
    lv_color_t fill = SP_COLOR_SURFACE_RAISED;

    if (kind == SP_BUTTON_PRIMARY) {
        accent = lv_color_hex(0x07141A);
        fill = sp_severity_color(SP_SEVERITY_NORMAL);
    } else if (kind == SP_BUTTON_DANGER) {
        accent = sp_severity_color(SP_SEVERITY_CRITICAL);
    }
    lv_obj_set_style_bg_color(button, fill, 0);
    lv_obj_set_style_text_color(button, accent, 0);
    lv_obj_set_style_border_color(button, kind == SP_BUTTON_NEUTRAL ? SP_COLOR_BORDER : accent, 0);
    if (kind == SP_BUTTON_PRIMARY) {
        lv_obj_set_style_border_color(button, fill, 0);
    }
}

lv_obj_t *sp_button_create(lv_obj_t *parent, const char *text, SpButtonKind kind, int32_t x,
                           int32_t y, int32_t width, int32_t height)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_t *label = lv_label_create(button);

    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, width, height);
    lv_obj_set_style_radius(button, 8, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_text_font(button, SP_FONT_BODY, 0);
    style_for_kind(button, kind);
    lv_obj_set_style_bg_opa(button, LV_OPA_50, LV_STATE_PRESSED);
    lv_obj_set_style_opa(button, LV_OPA_50, LV_STATE_DISABLED);
    lv_obj_set_style_text_color(button, SP_COLOR_TEXT_DIM, LV_STATE_DISABLED);

    lv_label_set_text(label, text);
    lv_obj_center(label);
    return button;
}

void sp_button_set_enabled(lv_obj_t *button, bool enabled)
{
    if (enabled) {
        lv_obj_remove_state(button, LV_STATE_DISABLED);
    } else {
        lv_obj_add_state(button, LV_STATE_DISABLED);
    }
}
