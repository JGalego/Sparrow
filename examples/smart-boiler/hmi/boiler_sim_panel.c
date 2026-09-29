#include "boiler_sim_panel.h"

#include "sp_button.h"
#include "sp_panel.h"
#include "sp_theme.h"

#define ROW_HEIGHT 32

static void inject(const BoilerScreenHost *host, BoilerPlantFault fault, uint8_t action)
{
    const BoilerFaultInjection injection = {.fault = (uint16_t)fault, .action = action};

    host->send_injection(host->context, &injection);
}

static void on_fault_toggled(lv_event_t *event)
{
    lv_obj_t *button = lv_event_get_target_obj(event);
    const BoilerScreenHost *host = lv_event_get_user_data(event);
    const BoilerPlantFault fault = (BoilerPlantFault)(uintptr_t)lv_obj_get_user_data(button);
    const bool on = lv_obj_has_state(button, LV_STATE_CHECKED);

    inject(host, fault, on ? 1 : 0);
}

static void on_clear_all(lv_event_t *event)
{
    lv_obj_t *panel = lv_obj_get_parent(lv_event_get_target_obj(event));
    lv_obj_t *rows = lv_obj_get_child(panel, 1);
    const BoilerScreenHost *host = lv_event_get_user_data(event);

    for (uint32_t i = 0; i < lv_obj_get_child_count(rows); i++) {
        lv_obj_remove_state(lv_obj_get_child(rows, (int32_t)i), LV_STATE_CHECKED);
    }
    inject(host, BOILER_PLANT_FAULT_NONE, 2);
}

lv_obj_t *boiler_sim_panel_create(lv_obj_t *parent, const BoilerScreenHost *host)
{
    const int32_t width = 244;
    const int32_t height = 44 + (BOILER_PLANT_FAULT_COUNT - 1) * ROW_HEIGHT + 56;
    lv_obj_t *panel = sp_panel_create(parent, "FAULT INJECTION", 768, 58, width, height);
    lv_obj_t *rows = lv_obj_create(panel);

    lv_obj_set_style_border_color(panel, sp_severity_color(SP_SEVERITY_WARNING), 0);
    sp_theme_make_plain(rows);
    lv_obj_set_pos(rows, 0, 36);
    lv_obj_set_size(rows, width, (BOILER_PLANT_FAULT_COUNT - 1) * ROW_HEIGHT + 4);
    for (int fault = 1; fault < BOILER_PLANT_FAULT_COUNT; fault++) {
        lv_obj_t *button = sp_button_create(rows, boiler_plant_fault_label((BoilerPlantFault)fault),
                                            SP_BUTTON_NEUTRAL, 12, (fault - 1) * ROW_HEIGHT,
                                            width - 24, ROW_HEIGHT - 4);
        lv_obj_add_flag(button, LV_OBJ_FLAG_CHECKABLE);
        lv_obj_set_style_bg_color(button, sp_severity_color(SP_SEVERITY_WARNING), LV_STATE_CHECKED);
        lv_obj_set_style_text_color(button, lv_color_hex(0x1A1204), LV_STATE_CHECKED);
        lv_obj_set_style_text_font(button, SP_FONT_SMALL, 0);
        lv_obj_set_user_data(button, (void *)(uintptr_t)fault);
        lv_obj_add_event_cb(button, on_fault_toggled, LV_EVENT_VALUE_CHANGED, (void *)host);
    }
    {
        lv_obj_t *clear =
            sp_button_create(panel, "Clear all", SP_BUTTON_DANGER, 12, height - 46, width - 24, 34);
        lv_obj_add_event_cb(clear, on_clear_all, LV_EVENT_CLICKED, (void *)host);
    }
    lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN);
    return panel;
}
