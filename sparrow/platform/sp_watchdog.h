#ifndef SPARROW_PLATFORM_WATCHDOG_H
#define SPARROW_PLATFORM_WATCHDOG_H

#include "sp_status.h"

/*
 * Linux watchdog device (/dev/watchdogN). Once opened, the hardware resets
 * the board unless it is kicked within its timeout. A clean close writes the
 * magic 'V' first so that a deliberate stop does not trigger a reset (on
 * drivers built without nowayout).
 */
typedef struct {
    int fd;
} SpWatchdog;

SpStatus sp_watchdog_open(SpWatchdog *watchdog, const char *path);
/* Returns SP_ERR_IO if the device rejected the kick; SP_OK without a device. */
SpStatus sp_watchdog_kick(const SpWatchdog *watchdog);
/* Returns SP_ERR_IO if the magic close failed; the watchdog then stays armed. */
SpStatus sp_watchdog_close(SpWatchdog *watchdog);

#endif
