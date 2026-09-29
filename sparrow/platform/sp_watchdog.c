#include "sp_watchdog.h"

#include <fcntl.h>
#include <unistd.h>

SpStatus sp_watchdog_open(SpWatchdog *watchdog, const char *path)
{
    watchdog->fd = open(path, O_WRONLY | O_CLOEXEC);
    return watchdog->fd >= 0 ? SP_OK : SP_ERR_IO;
}

void sp_watchdog_kick(const SpWatchdog *watchdog)
{
    if (watchdog->fd >= 0) {
        (void)write(watchdog->fd, "k", 1);
    }
}

void sp_watchdog_close(SpWatchdog *watchdog)
{
    if (watchdog->fd >= 0) {
        (void)write(watchdog->fd, "V", 1);
        close(watchdog->fd);
        watchdog->fd = -1;
    }
}
