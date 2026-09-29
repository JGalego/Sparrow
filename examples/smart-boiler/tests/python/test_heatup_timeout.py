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
