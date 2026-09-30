#ifndef GEAR_CAPI_H
#define GEAR_CAPI_H

#include "gear_controller.h"

/* Heap-allocates and initializes a controller. Returns NULL and sets *status on failure. */
GearController *gear_capi_create(const GearConfig *config, int *status);
void gear_capi_destroy(GearController *controller);

#endif
