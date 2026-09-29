#ifndef BOILER_COMMANDS_H
#define BOILER_COMMANDS_H

#include "boiler_model.h"

/*
 * Combines operator commands that arrived between two control steps into
 * the one BoilerCommands the next step consumes. Requests are sticky until
 * consumed: a start sent twice is one start, and a later setpoint or manual
 * request replaces an earlier one. A stop cancels any start in the same
 * window, whichever arrived first.
 */
void boiler_commands_merge(BoilerCommands *pending, const BoilerCommands *received);

#endif
