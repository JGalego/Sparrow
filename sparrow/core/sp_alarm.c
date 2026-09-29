#include "sp_alarm.h"

void sp_alarms_init(SpAlarmSet *alarms)
{
    alarms->active = 0;
    alarms->latched = 0;
    alarms->unacked = 0;
}

void sp_alarms_update(SpAlarmSet *alarms, unsigned index, bool present, bool latching)
{
    const uint32_t bit = SP_ALARM_BIT(index);

    if (!present) {
        alarms->active &= ~bit;
        return;
    }
    if ((alarms->active & bit) == 0) {
        alarms->unacked |= bit;
    }
    alarms->active |= bit;
    if (latching) {
        alarms->latched |= bit;
    }
}

void sp_alarms_ack_all(SpAlarmSet *alarms)
{
    alarms->unacked = 0;
}

bool sp_alarms_reset(SpAlarmSet *alarms)
{
    alarms->latched &= alarms->active;
    return alarms->latched == 0;
}

uint32_t sp_alarms_standing(const SpAlarmSet *alarms)
{
    return alarms->active | alarms->latched;
}
