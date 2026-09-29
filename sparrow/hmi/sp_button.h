#ifndef SPARROW_HMI_BUTTON_H
#define SPARROW_HMI_BUTTON_H

#include "lvgl.h"

typedef enum {
    SP_BUTTON_NEUTRAL,
    SP_BUTTON_PRIMARY, /* the expected next step, filled */
    SP_BUTTON_DANGER
} SpButtonKind;

/* A text button. Disabled buttons are dimmed and ignore input (LV_STATE_DISABLED). */
lv_obj_t *sp_button_create(lv_obj_t *parent, const char *text, SpButtonKind kind, int32_t x,
                           int32_t y, int32_t width, int32_t height);

void sp_button_set_enabled(lv_obj_t *button, bool enabled);

#endif
