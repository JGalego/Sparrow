#ifndef SPARROW_CORE_PI_H
#define SPARROW_CORE_PI_H

/* PI regulator with output clamping and conditional-integration anti-windup. */
typedef struct {
    float kp;
    float ki;
    float out_min;
    float out_max;
    float integral;
} SpPi;

void sp_pi_init(SpPi *pi, float kp, float ki, float out_min, float out_max);
void sp_pi_reset(SpPi *pi);

/* error = setpoint - measurement, dt_s = step length in seconds. */
float sp_pi_step(SpPi *pi, float error, float dt_s);

#endif
