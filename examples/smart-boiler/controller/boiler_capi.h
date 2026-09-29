#ifndef BOILER_CAPI_H
#define BOILER_CAPI_H

#include "boiler_controller.h"

/* Heap-allocates and initializes a controller. Returns NULL and sets *status on failure. */
BoilerController *boiler_capi_create(const BoilerConfig *config, int *status);
void boiler_capi_destroy(BoilerController *controller);

#endif
