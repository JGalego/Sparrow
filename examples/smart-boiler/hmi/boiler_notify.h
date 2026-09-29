#ifndef BOILER_NOTIFY_H
#define BOILER_NOTIFY_H

#include <stddef.h>
#include <stdint.h>

#include "boiler_model.h"
#include "sp_severity.h"

#define BOILER_NOTIFY_CAPACITY 64
#define BOILER_NOTIFY_TEXT     48

typedef struct {
    uint32_t uptime_ms; /* controller time of the event */
    SpSeverity severity;
    char text[BOILER_NOTIFY_TEXT];
} BoilerNotification;

/* Fixed-size log; the oldest entry is dropped when it is full. */
typedef struct {
    BoilerNotification items[BOILER_NOTIFY_CAPACITY];
    size_t head;
    size_t count;
} BoilerNotifications;

void boiler_notify_init(BoilerNotifications *log);
void boiler_notify_add(BoilerNotifications *log, uint32_t uptime_ms, SpSeverity severity,
                       const char *text);
size_t boiler_notify_count(const BoilerNotifications *log);

/* age 0 is the newest entry; returns NULL beyond the stored entries. */
const BoilerNotification *boiler_notify_get(const BoilerNotifications *log, size_t age);

/*
 * Logs what changed between two consecutive status frames: state changes,
 * alarms that became standing or cleared, and acknowledgement.
 */
void boiler_notify_observe(BoilerNotifications *log, const BoilerStatus *previous,
                           const BoilerStatus *current);

#endif
