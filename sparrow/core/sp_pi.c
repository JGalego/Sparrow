#include "sp_pi.h"
#include <stdbool.h>

#include "sp_util.h"

void sp_pi_init(SpPi *pi, float kp, float ki, float out_min, float out_max)
{
    pi->kp = kp;
    pi->ki = ki;
    pi->out_min = out_min;
    pi->out_max = out_max;
    pi->integral = 0.0f;
}

void sp_pi_reset(SpPi *pi)
{
    pi->integral = 0.0f;
}

float sp_pi_step(SpPi *pi, float error, float dt_s)
{
    const float integral = pi->integral + pi->ki * error * dt_s;
    const float unclamped = pi->kp * error + integral;
    const float output = sp_clampf(unclamped, pi->out_min, pi->out_max);
    const bool saturated_high = unclamped > pi->out_max && error > 0.0f;
    const bool saturated_low = unclamped < pi->out_min && error < 0.0f;

    if (!saturated_high && !saturated_low) {
        pi->integral = sp_clampf(integral, pi->out_min, pi->out_max);
    }
    return output;
}
