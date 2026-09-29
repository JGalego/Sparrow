/*
 * Allocation wrapper for hosts that cannot place a BoilerController themselves
 * (the Python simulator loads this library through ctypes). Not used on the
 * target, where the application owns a static controller instance.
 */
#include <stdlib.h>

#include "boiler_capi.h"

BoilerController *boiler_capi_create(const BoilerConfig *config, int *status)
{
    BoilerController *controller = malloc(sizeof *controller);

    if (controller == NULL) {
        *status = SP_ERR_IO;
        return NULL;
    }
    *status = boiler_init(controller, config, NULL);
    if (*status != SP_OK) {
        free(controller);
        return NULL;
    }
    return controller;
}

void boiler_capi_destroy(BoilerController *controller)
{
    free(controller);
}
