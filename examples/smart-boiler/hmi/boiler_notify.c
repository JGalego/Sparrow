#include "boiler_notify.h"

#include <stdio.h>
#include <string.h>

void boiler_notify_init(BoilerNotifications *log)
{
    memset(log, 0, sizeof *log);
}

void boiler_notify_add(BoilerNotifications *log, uint32_t uptime_ms, SpSeverity severity,
                       const char *text)
{
    BoilerNotification *entry = &log->items[log->head];

    entry->uptime_ms = uptime_ms;
    entry->severity = severity;
    snprintf(entry->text, sizeof entry->text, "%s", text);
    log->head = (log->head + 1) % BOILER_NOTIFY_CAPACITY;
    if (log->count < BOILER_NOTIFY_CAPACITY) {
        log->count++;
    }
}

size_t boiler_notify_count(const BoilerNotifications *log)
{
    return log->count;
}

const BoilerNotification *boiler_notify_get(const BoilerNotifications *log, size_t age)
{
    if (age >= log->count) {
        return NULL;
    }
    return &log->items[(log->head + BOILER_NOTIFY_CAPACITY - 1 - age) % BOILER_NOTIFY_CAPACITY];
}

static SpSeverity state_severity(BoilerState state)
{
    return state == BOILER_STATE_FAULT ? SP_SEVERITY_CRITICAL : SP_SEVERITY_INFO;
}

static SpSeverity fault_severity(const BoilerFaultInfo *info)
{
    return info->severity == BOILER_SEVERITY_CRITICAL ? SP_SEVERITY_CRITICAL : SP_SEVERITY_WARNING;
}

static void observe_state(BoilerNotifications *log, const BoilerStatus *previous,
                          const BoilerStatus *current)
{
    char text[BOILER_NOTIFY_TEXT];

    if (previous->state == current->state) {
        return;
    }
    snprintf(text, sizeof text, "State: %s", boiler_state_label((BoilerState)current->state));
    boiler_notify_add(log, current->uptime_ms, state_severity((BoilerState)current->state), text);
}

static void observe_alarms(BoilerNotifications *log, const BoilerStatus *previous,
                           const BoilerStatus *current)
{
    const uint32_t before = previous->alarms_active | previous->alarms_latched;
    const uint32_t after = current->alarms_active | current->alarms_latched;

    for (int fault = 0; fault < BOILER_FAULT_COUNT; fault++) {
        const uint32_t bit = BOILER_FAULT_BIT(fault);
        const BoilerFaultInfo *info = boiler_fault_info((BoilerFault)fault);
        char text[BOILER_NOTIFY_TEXT];

        if ((after & bit) && !(before & bit)) {
            snprintf(text, sizeof text, "%s", info->text);
            boiler_notify_add(log, current->uptime_ms, fault_severity(info), text);
        } else if ((before & bit) && !(after & bit)) {
            snprintf(text, sizeof text, "%s cleared", info->text);
            boiler_notify_add(log, current->uptime_ms, SP_SEVERITY_NORMAL, text);
        }
    }
}

void boiler_notify_observe(BoilerNotifications *log, const BoilerStatus *previous,
                           const BoilerStatus *current)
{
    observe_alarms(log, previous, current);
    observe_state(log, previous, current);
    if (previous->alarms_unacked != 0 && current->alarms_unacked == 0) {
        boiler_notify_add(log, current->uptime_ms, SP_SEVERITY_NORMAL, "Alarms acknowledged");
    }
}
