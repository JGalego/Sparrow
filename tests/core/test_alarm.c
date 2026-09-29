#include "sp_alarm.h"
#include "sp_test.h"

enum { ALARM_A = 0, ALARM_B = 3 };

SP_TEST(raising_sets_active_and_unacked, "")
{
    SpAlarmSet alarms;
    sp_alarms_init(&alarms);

    sp_alarms_update(&alarms, ALARM_A, true, false);

    SP_ASSERT_EQ_INT(SP_ALARM_BIT(ALARM_A), alarms.active);
    SP_ASSERT_EQ_INT(SP_ALARM_BIT(ALARM_A), alarms.unacked);
    SP_ASSERT_EQ_INT(0, alarms.latched);
}

SP_TEST(non_latching_alarm_clears_with_condition, "")
{
    SpAlarmSet alarms;
    sp_alarms_init(&alarms);
    sp_alarms_update(&alarms, ALARM_A, true, false);

    sp_alarms_update(&alarms, ALARM_A, false, false);

    SP_ASSERT_EQ_INT(0, alarms.active);
    SP_ASSERT_EQ_INT(0, sp_alarms_standing(&alarms));
}

SP_TEST(latching_alarm_survives_cleared_condition, "REQ-015")
{
    SpAlarmSet alarms;
    sp_alarms_init(&alarms);
    sp_alarms_update(&alarms, ALARM_B, true, true);

    sp_alarms_update(&alarms, ALARM_B, false, true);

    SP_ASSERT_EQ_INT(0, alarms.active);
    SP_ASSERT_EQ_INT(SP_ALARM_BIT(ALARM_B), alarms.latched);
}

SP_TEST(reset_clears_latched_alarm_only_when_condition_is_gone, "REQ-015")
{
    SpAlarmSet alarms;
    sp_alarms_init(&alarms);
    sp_alarms_update(&alarms, ALARM_A, true, true);
    sp_alarms_update(&alarms, ALARM_B, true, true);
    sp_alarms_update(&alarms, ALARM_B, false, true);

    const bool all_cleared = sp_alarms_reset(&alarms);

    SP_ASSERT(!all_cleared);
    SP_ASSERT_EQ_INT(SP_ALARM_BIT(ALARM_A), alarms.latched);
}

SP_TEST(reset_reports_success_when_nothing_remains, "REQ-015")
{
    SpAlarmSet alarms;
    sp_alarms_init(&alarms);
    sp_alarms_update(&alarms, ALARM_A, true, true);
    sp_alarms_update(&alarms, ALARM_A, false, true);

    SP_ASSERT(sp_alarms_reset(&alarms));
    SP_ASSERT_EQ_INT(0, alarms.latched);
}

SP_TEST(ack_clears_unacked_but_keeps_alarm_active, "REQ-019")
{
    SpAlarmSet alarms;
    sp_alarms_init(&alarms);
    sp_alarms_update(&alarms, ALARM_A, true, false);

    sp_alarms_ack_all(&alarms);

    SP_ASSERT_EQ_INT(0, alarms.unacked);
    SP_ASSERT_EQ_INT(SP_ALARM_BIT(ALARM_A), alarms.active);
}

SP_TEST(repeated_update_does_not_unacknowledge, "REQ-019")
{
    SpAlarmSet alarms;
    sp_alarms_init(&alarms);
    sp_alarms_update(&alarms, ALARM_A, true, false);
    sp_alarms_ack_all(&alarms);

    sp_alarms_update(&alarms, ALARM_A, true, false);

    SP_ASSERT_EQ_INT(0, alarms.unacked);
}

SP_TEST(alarm_that_returns_after_clearing_is_unacknowledged_again, "REQ-019")
{
    SpAlarmSet alarms;
    sp_alarms_init(&alarms);
    sp_alarms_update(&alarms, ALARM_A, true, false);
    sp_alarms_ack_all(&alarms);
    sp_alarms_update(&alarms, ALARM_A, false, false);

    sp_alarms_update(&alarms, ALARM_A, true, false);

    SP_ASSERT_EQ_INT(SP_ALARM_BIT(ALARM_A), alarms.unacked);
}
