#ifndef BOILER_SEQUENCE_H
#define BOILER_SEQUENCE_H

#include "boiler_controller.h"

/*
 * Runs the state machine for one step: applies operator commands, performs
 * state transitions and sets controller->outputs (except the alarm horn).
 * Alarms must already be up to date for this step.
 */
void boiler_sequence_step(BoilerController *controller, const BoilerCommands *commands,
                          uint32_t dt_ms);

#endif
