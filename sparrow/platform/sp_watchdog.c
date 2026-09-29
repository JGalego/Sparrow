#include "sp_watchdog.h"

#include <fcntl.h>
#include <unistd.h>

SpStatus sp_watchdog_open(SpWatchdog *watchdog, const char *path)
{
    watchdog->fd = open(path, O_WRONLY | O_CLOEXEC);
    return watchdog->fd >= 0 ? SP_OK : SP_ERR_IO;
}

SpStatus sp_watchdog_kick(const SpWatchdog *watchdog)
{
    if (watchdog->fd < 0) {
        return SP_OK;
    }
    return write(watchdog->fd, "k", 1) == 1 ? SP_OK : SP_ERR_IO;
}

SpStatus sp_watchdog_close(SpWatchdog *watchdog)
{
    SpStatus status = SP_OK;

    if (watchdog->fd < 0) {
        return SP_OK;
    }
    if (write(watchdog->fd, "V", 1) != 1) {
        status = SP_ERR_IO; /* the driver stays armed and will reset the board */
    }
    close(watchdog->fd);
    watchdog->fd = -1;
    return status;
}
