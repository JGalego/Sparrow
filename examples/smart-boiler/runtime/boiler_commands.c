#include "boiler_commands.h"

static uint8_t either(uint8_t a, uint8_t b)
{
    return (a != 0 || b != 0) ? 1 : 0;
}

void boiler_commands_merge(BoilerCommands *pending, const BoilerCommands *received)
{
    pending->start = either(pending->start, received->start);
    pending->stop = either(pending->stop, received->stop);
    pending->ack = either(pending->ack, received->ack);
    pending->reset = either(pending->reset, received->reset);
    if (received->set_setpoint) {
        pending->set_setpoint = 1;
        pending->setpoint_c = received->setpoint_c;
    }
    if (received->pump_manual) {
        pending->pump_manual = received->pump_manual;
    }
    if (received->valve_manual) {
        pending->valve_manual = received->valve_manual;
    }
    if (pending->stop) {
        pending->start = 0;
    }
}
