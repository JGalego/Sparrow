import pytest


@pytest.mark.verifies("REQ-042")
@pytest.mark.parametrize(
    "initial_temperature_c",
    [20.0, 90.0],
    ids=["cold-plant", "initially-above-setpoint"],
)
def test_heatup_monitoring_does_not_interrupt_closed_loop_operation(
    make_bench, initial_temperature_c
):
    """Exercise real thermal dynamics beyond the timeout, without a state change.

    Exact alarm and completion boundaries are tested with controlled C inputs.
    These cases retain the physical plant: one heats from cold, while the other
    starts above the reference 80 degC setpoint and subsequently cools/regulates.
    """
    bench = make_bench(initial_temperature_c=initial_temperature_c)
    bench.run(2)
    bench.command(start=1)
    assert bench.run_until(lambda b: b.state.name == "RUNNING", 30)

    # Observe every bench step, rather than checking only the final state.
    assert not bench.run_until(lambda b: b.state.name != "RUNNING", 1201)
    assert bench.state.name == "RUNNING"


@pytest.mark.verifies("REQ-042")
def test_degraded_heater_raises_the_warning_just_after_twenty_minutes(make_bench):
    """Added during review: the positive closed-loop case, with a 4 kW heater."""
    from smart_boiler.model import Fault, State

    bench = make_bench(initial_temperature_c=20.0, heater_power_w=4000.0)
    bench.run(2)
    bench.command(start=1)
    bench.step()
    assert bench.state is State.STARTUP
    started_ms = bench.time_ms

    assert bench.run_until(lambda b: b.raised(Fault.HEATUP_TIMEOUT), 1300)

    assert bench.time_ms - started_ms == 1_200_100
    assert bench.state is State.RUNNING
    assert bench.status.alarms_active == 1 << Fault.HEATUP_TIMEOUT
    assert bench.status.heater_contactor == 1


@pytest.mark.verifies("REQ-042")
@pytest.mark.parametrize("setpoint_delta_c", [-1.0, 1.0], ids=["lower-target", "raise-target"])
def test_setpoint_change_during_slow_heatup_keeps_original_deadline(make_bench, setpoint_delta_c):
    """An incomplete physical heat-up retains its deadline after a target change."""
    from smart_boiler.model import Fault, State

    bench = make_bench(initial_temperature_c=20.0, heater_power_w=4000.0)
    bench.run(2)
    bench.command(start=1)
    bench.step()
    assert bench.state is State.STARTUP
    started_ms = bench.time_ms

    bench.run(600)
    assert bench.state is State.RUNNING
    assert not bench.raised(Fault.HEATUP_TIMEOUT)
    target_c = bench.status.setpoint_c + setpoint_delta_c
    assert bench.status.temperature_valid == 1
    assert bench.status.temperature_c < min(bench.status.setpoint_c, target_c)

    bench.command(set_setpoint=1, setpoint_c=target_c)
    bench.step()
    assert bench.status.setpoint_c == pytest.approx(target_c)
    assert not bench.raised(Fault.HEATUP_TIMEOUT)

    assert bench.run_until(lambda b: b.raised(Fault.HEATUP_TIMEOUT), 700)
    assert bench.time_ms - started_ms == 1_200_100
    assert bench.state is State.RUNNING
    assert bench.status.alarms_active == 1 << Fault.HEATUP_TIMEOUT
    assert bench.status.alarms_latched & (1 << Fault.HEATUP_TIMEOUT) == 0
    assert bench.status.heater_contactor == 1
    assert bench.status.alarm_horn == 0


@pytest.mark.verifies("REQ-042")
def test_stop_clears_slow_heatup_warning_and_keeps_it_inactive_while_cooling(make_bench):
    """Observe cancellation throughout real cooling, beyond another timeout interval."""
    from smart_boiler.model import Fault, State

    bench = make_bench(initial_temperature_c=20.0, heater_power_w=4000.0)
    bench.run(2)
    bench.command(start=1)
    bench.step()
    assert bench.run_until(lambda b: b.raised(Fault.HEATUP_TIMEOUT), 1300)
    assert bench.state is State.RUNNING
    temperature_before_stop_c = bench.status.temperature_c

    bench.command(stop=1)
    bench.step()
    assert bench.state in (State.SHUTDOWN, State.STANDBY)
    assert not bench.raised(Fault.HEATUP_TIMEOUT)
    assert bench.status.alarms_latched & (1 << Fault.HEATUP_TIMEOUT) == 0
    assert bench.status.heater_contactor == 0
    assert bench.status.heater_power_pct == pytest.approx(0.0)

    # Check every physical plant step, including the transition out of SHUTDOWN.
    assert not bench.run_until(
        lambda b: (
            b.raised(Fault.HEATUP_TIMEOUT)
            or b.state not in (State.SHUTDOWN, State.STANDBY)
            or b.status.heater_contactor != 0
            or b.status.heater_power_pct != 0.0
        ),
        1201,
    )
    assert bench.status.temperature_valid == 1
    assert bench.status.temperature_c < temperature_before_stop_c
