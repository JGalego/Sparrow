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
