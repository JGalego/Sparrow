/*
 * Allocation wrapper for hosts that cannot place a GearController themselves
 * (the Python simulator loads this library through ctypes). Not used on the
 * target, where the application owns a static controller instance.
 */
#include <stdlib.h>

#include "gear_capi.h"

GearController *gear_capi_create(const GearConfig *config, int *status)
{
    GearController *controller = malloc(sizeof *controller);

    if (controller == NULL) {
        *status = SP_ERR_IO;
        return NULL;
    }
    *status = gear_init(controller, config, NULL);
    if (*status != SP_OK) {
        free(controller);
        return NULL;
    }
    return controller;
}

void gear_capi_destroy(GearController *controller)
{
    free(controller);
}
