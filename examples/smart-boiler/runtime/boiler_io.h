#ifndef BOILER_IO_H
#define BOILER_IO_H

#include <stdbool.h>

#include "boiler_model.h"
#include "sp_status.h"

/*
 * Hardware abstraction for the boiler's field I/O. A backend reads the
 * transmitters and the pump feedback, and drives the actuators.
 *
 * read() must not block for longer than a small fraction of the control
 * period, and returns whether the inputs are fresh. When a backend cannot
 * produce fresh inputs it must return open-loop values (all loop currents
 * 0 mA, pump feedback 0) rather than repeating old ones: the controller then
 * raises sensor faults and moves to its fault state, which is the defined
 * reaction to lost I/O.
 */
typedef struct {
    bool (*read)(void *context, BoilerInputs *inputs);
    void (*write)(void *context, const BoilerOutputs *outputs);
    void *context;
} BoilerIo;

/* Inputs a backend returns when its source is lost. */
static inline BoilerInputs boiler_io_open_loop(void)
{
    const BoilerInputs inputs = {0};

    return inputs;
}

/* Outputs written on shutdown: heater off, pump off, valve closed, horn off. */
static inline BoilerOutputs boiler_io_safe_outputs(void)
{
    const BoilerOutputs outputs = {0};

    return outputs;
}

#endif
