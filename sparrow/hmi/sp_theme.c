#include "sp_theme.h"

lv_color_t sp_severity_color(SpSeverity severity)
{
    switch (severity) {
    case SP_SEVERITY_NORMAL:
        return lv_color_hex(0x34D399);
    case SP_SEVERITY_INFO:
        return lv_color_hex(0x4FA8F0);
    case SP_SEVERITY_WARNING:
        return lv_color_hex(0xF5B83D);
    case SP_SEVERITY_CRITICAL:
        return lv_color_hex(0xFF5C5C);
    case SP_SEVERITY_INACTIVE:
        break;
    }
    return lv_color_hex(0x5A6675);
}

void sp_theme_make_plain(lv_obj_t *obj)
{
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(obj, 0, 0);
}

lv_obj_t *sp_label_create(lv_obj_t *parent, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *label = lv_label_create(parent);

    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    return label;
}
