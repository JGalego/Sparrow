#include "sp_notification_list.h"

#include "sp_theme.h"

#define ROW_HEIGHT 42

/* Row r is child r; its children are: dot, message, time. */
lv_obj_t *sp_notification_list_create(lv_obj_t *parent, int32_t x, int32_t y, int32_t width,
                                      int32_t height, int rows)
{
    lv_obj_t *list = lv_obj_create(parent);

    sp_theme_make_plain(list);
    lv_obj_set_pos(list, x, y);
    lv_obj_set_size(list, width, height);
    for (int i = 0; i < rows; i++) {
        lv_obj_t *row = lv_obj_create(list);
        lv_obj_t *dot = lv_obj_create(row);
        lv_obj_t *message = sp_label_create(row, SP_FONT_BODY, SP_COLOR_TEXT);
        lv_obj_t *time = sp_label_create(row, SP_FONT_SMALL, SP_COLOR_TEXT_DIM);

        sp_theme_make_plain(row);
        lv_obj_set_pos(row, 0, i * ROW_HEIGHT);
        lv_obj_set_size(row, width, ROW_HEIGHT);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_color(row, SP_COLOR_BORDER, 0);
        lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);

        sp_theme_make_plain(dot);
        lv_obj_set_size(dot, 8, 8);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_pos(dot, 2, 9);

        lv_obj_set_width(message, width - 20);
        lv_label_set_long_mode(message, LV_LABEL_LONG_DOT);
        lv_obj_set_pos(message, 18, 3);
        lv_obj_set_pos(time, 18, 22);
    }
    return list;
}

void sp_notification_list_set_row(lv_obj_t *list, int row, const char *text, const char *time_text,
                                  SpSeverity severity)
{
    lv_obj_t *item = lv_obj_get_child(list, row);

    if (text == NULL) {
        lv_obj_add_flag(item, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_remove_flag(item, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(lv_obj_get_child(item, 0), sp_severity_color(severity), 0);
    lv_label_set_text(lv_obj_get_child(item, 1), text);
    lv_label_set_text(lv_obj_get_child(item, 2), time_text);
}
