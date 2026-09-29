#ifndef BOILER_PROCESS_VIEW_H
#define BOILER_PROCESS_VIEW_H

#include "boiler_view.h"
#include "lvgl.h"

/*
 * Schematic of the boiler circuit: vessel with water level and heater
 * element, pump, valve and the pipe between them with animated flow.
 */
lv_obj_t *boiler_process_view_create(lv_obj_t *parent, int32_t x, int32_t y, int32_t width,
                                     int32_t height);

void boiler_process_view_update(lv_obj_t *view, const BoilerViewModel *model);

#endif
