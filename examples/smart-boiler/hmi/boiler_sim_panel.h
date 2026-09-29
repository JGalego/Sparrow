#ifndef BOILER_SIM_PANEL_H
#define BOILER_SIM_PANEL_H

#include "boiler_screen.h"

/* Overlay listing the plant faults as toggles. Hidden until shown. */
lv_obj_t *boiler_sim_panel_create(lv_obj_t *parent, const BoilerScreenHost *host);

#endif
