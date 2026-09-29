#ifndef SPARROW_CORE_ALARM_H
#define SPARROW_CORE_ALARM_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Alarm state for up to 32 alarms, one bit per alarm. Bit positions are
 * assigned by the application (see the generated fault enumeration).
 *
 *   active   the condition is present now
 *   latched  the condition was present and has not been reset yet
 *   unacked  the operator has not acknowledged the alarm
 */
typedef struct {
    uint32_t active;
    uint32_t latched;
    uint32_t unacked;
} SpAlarmSet;

#define SP_ALARM_BIT(index) (UINT32_C(1) << (index))

void sp_alarms_init(SpAlarmSet *alarms);

/* Records the current condition of one alarm. A latching alarm stays latched after the condition
 * clears. */
void sp_alarms_update(SpAlarmSet *alarms, unsigned index, bool present, bool latching);

/* Marks every alarm as acknowledged. */
void sp_alarms_ack_all(SpAlarmSet *alarms);

/*
 * Clears latched alarms whose condition is gone. Returns true if no latched
 * alarm remains, false if a latched alarm is still active and was kept.
 */
bool sp_alarms_reset(SpAlarmSet *alarms);

/* Alarms that are active or latched. */
uint32_t sp_alarms_standing(const SpAlarmSet *alarms);

#endif
