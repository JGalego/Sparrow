"""Controller (C library) against the plant model."""

import pytest
from smart_boiler.bench import BoilerBench
from smart_boiler.model import Fault, State
from smart_boiler.plant import BoilerParams


def peak_temperature(bench, seconds):
    peak = bench.plant.temperature_c
    for _ in range(round(seconds * 10)):
        bench.step()
        peak = max(peak, bench.plant.temperature_c)
    return peak


@pytest.mark.verifies("REQ-003")
def test_startup_reaches_running_within_a_minute(bench):
    bench.run(2)
    bench.command(start=1)

    assert bench.run_until(lambda b: b.state is State.RUNNING, 60)
    assert bench.plant.valve_position_pct == 100.0
    assert bench.plant.flow_lpm > 20.0


@pytest.mark.verifies("REQ-003")
def test_pump_starts_only_after_the_valve_has_opened(bench):
    bench.run(2)
    bench.command(start=1)

    bench.run(4)

    assert bench.state is State.STARTUP
    assert bench.status.pump_on == 0
    assert bench.plant.valve_position_pct < 90.0


@pytest.mark.verifies("REQ-004")
def test_temperature_settles_within_two_degrees_of_the_setpoint(make_bench):
    bench = make_bench(initial_temperature_c=60.0)
    bench.run(2)
    bench.command(start=1)
    assert bench.run_until(lambda b: b.state is State.RUNNING, 30)

    bench.run(600)
    late = bench.recorder.column("temperature_c")[-600:]

    assert all(abs(t - 80.0) <= 2.0 for t in late), (min(late), max(late))


@pytest.mark.verifies("REQ-004")
def test_overshoot_stays_below_three_degrees(make_bench):
    bench = make_bench(initial_temperature_c=50.0)
    bench.run(2)
    bench.command(start=1)
    assert bench.run_until(lambda b: b.state is State.RUNNING, 30)

    assert peak_temperature(bench, 900) < 83.0


@pytest.mark.verifies("REQ-004,REQ-005")
def test_setpoint_change_is_followed(running_bench):
    running_bench.command(set_setpoint=1, setpoint_c=70.0)

    running_bench.run(900)

    assert running_bench.status.setpoint_c == 70.0
    assert running_bench.plant.temperature_c == pytest.approx(70.0, abs=2.0)


@pytest.mark.verifies("REQ-004")
def test_regulation_works_with_noisy_sensors(make_bench):
    bench = make_bench(initial_temperature_c=78.0, noise=True, seed=3)
    bench.run(2)
    bench.command(start=1)
    assert bench.run_until(lambda b: b.state is State.RUNNING, 30)

    bench.run(300)
    late = bench.recorder.column("temperature_c")[-300:]

    assert all(abs(t - 80.0) <= 2.0 for t in late)


@pytest.mark.verifies("REQ-017")
def test_shutdown_cools_the_boiler_and_returns_to_standby(running_bench):
    running_bench.command(stop=1)

    assert running_bench.run_until(lambda b: b.state is State.STANDBY, 1200)
    running_bench.run(1)
    assert running_bench.plant.temperature_c <= 55.5
    assert running_bench.status.pump_on == 0
    assert running_bench.status.heater_contactor == 0


# Fault injection


@pytest.mark.verifies("REQ-006,REQ-016")
def test_shorted_heater_relay_is_stopped_by_the_over_temperature_trip(running_bench):
    running_bench.inject("HEATER_SSR_SHORTED")

    tripped = running_bench.run_until(lambda b: b.raised(Fault.OVER_TEMP), 600)

    assert tripped
    assert running_bench.state is State.FAULT
    assert running_bench.status.heater_contactor == 0
    assert peak_temperature(running_bench, 300) < 115.0


@pytest.mark.verifies("REQ-006,REQ-016")
def test_over_temperature_trip_keeps_the_water_circulating_while_hot(running_bench):
    running_bench.inject("HEATER_SSR_SHORTED")
    running_bench.run_until(lambda b: b.raised(Fault.OVER_TEMP), 600)

    running_bench.run(30)

    assert running_bench.status.pump_on == 1
    assert running_bench.plant.flow_lpm > 30.0


@pytest.mark.verifies("REQ-007")
def test_temperature_warning_precedes_the_trip(running_bench):
    running_bench.inject("HEATER_SSR_SHORTED")

    running_bench.run_until(lambda b: b.raised(Fault.OVER_TEMP), 600)
    warning_time = next(
        row["time_s"]
        for row in running_bench.recorder.rows
        if int(row["alarms_active"]) & (1 << Fault.TEMP_HIGH)
    )

    assert warning_time < running_bench.time_s
    assert running_bench.raised(Fault.TEMP_HIGH)


@pytest.mark.verifies("REQ-002")
@pytest.mark.parametrize(
    ("plant_fault", "controller_fault"),
    [
        ("TEMP_SENSOR_OPEN", Fault.TEMP_SENSOR),
        ("TEMP_SENSOR_SHORT", Fault.TEMP_SENSOR),
        ("PRESSURE_SENSOR_OPEN", Fault.PRESSURE_SENSOR),
        ("FLOW_SENSOR_OPEN", Fault.FLOW_SENSOR),
        ("VALVE_SENSOR_OPEN", Fault.VALVE_SENSOR),
    ],
)
def test_sensor_failure_is_detected_and_stops_the_heater(
    running_bench, plant_fault, controller_fault
):
    running_bench.inject(plant_fault)

    running_bench.run(2)

    assert running_bench.raised(controller_fault)
    assert running_bench.state is State.FAULT
    assert running_bench.status.heater_contactor == 0


@pytest.mark.verifies("REQ-002,REQ-013")
def test_heater_is_cut_within_one_step_of_a_temperature_sensor_failure(running_bench):
    running_bench.inject("TEMP_SENSOR_OPEN")

    running_bench.step()

    assert running_bench.status.heater_contactor == 0
    assert running_bench.state is State.RUNNING


@pytest.mark.verifies("REQ-011")
def test_tripped_pump_is_detected(running_bench):
    running_bench.inject("PUMP_TRIPPED")

    assert running_bench.run_until(lambda b: b.raised(Fault.PUMP_FAILURE), 10)
    assert running_bench.state is State.FAULT
    assert running_bench.status.heater_contactor == 0


@pytest.mark.verifies("REQ-011,REQ-013")
def test_heater_is_cut_immediately_when_the_pump_trips(running_bench):
    running_bench.inject("PUMP_TRIPPED")

    running_bench.run(0.3)

    assert running_bench.status.heater_contactor == 0


@pytest.mark.verifies("REQ-012")
def test_seized_pump_is_detected_as_loss_of_flow(running_bench):
    running_bench.inject("PUMP_SEIZED")

    assert running_bench.run_until(lambda b: b.raised(Fault.NO_FLOW), 15)
    assert running_bench.state is State.FAULT
    assert running_bench.status.heater_contactor == 0


@pytest.mark.verifies("REQ-012,REQ-016")
def test_no_flow_fault_does_not_keep_the_pump_running(running_bench):
    running_bench.inject("PUMP_SEIZED")
    running_bench.run_until(lambda b: b.raised(Fault.NO_FLOW), 15)

    running_bench.run(5)

    assert running_bench.status.pump_on == 0


@pytest.mark.verifies("REQ-014")
def test_valve_stuck_closed_during_startup_is_detected(bench):
    bench.run(2)
    bench.inject("VALVE_STUCK")
    bench.command(start=1)

    assert bench.run_until(lambda b: b.raised(Fault.VALVE_FAILURE), 30)
    assert bench.state is State.FAULT


@pytest.mark.verifies("REQ-014")
def test_stuck_valve_never_lets_the_pump_run_against_it(bench):
    bench.run(2)
    bench.inject("VALVE_STUCK")
    bench.command(start=1)

    bench.run(40)

    assert bench.status.pump_on == 0
    assert bench.plant.pressure_bar < 1.5


@pytest.mark.verifies("REQ-014")
def test_valve_stuck_open_is_detected_once_it_is_commanded_closed(running_bench):
    running_bench.inject("VALVE_STUCK")
    running_bench.command(stop=1)

    assert running_bench.run_until(lambda b: b.raised(Fault.VALVE_FAILURE), 1500)
    assert running_bench.state is State.FAULT


def start_with_failed_expansion_vessel(make_bench):
    bench = make_bench(initial_temperature_c=40.0)
    bench.inject("EXPANSION_VESSEL_FAILED")
    bench.run(2)
    bench.command(start=1)
    return bench


@pytest.mark.verifies("REQ-008,REQ-016")
def test_failed_expansion_vessel_trips_on_over_pressure(make_bench):
    bench = start_with_failed_expansion_vessel(make_bench)

    assert bench.run_until(lambda b: b.raised(Fault.OVER_PRESSURE), 600)

    assert bench.state is State.FAULT
    assert bench.status.heater_contactor == 0
    assert bench.plant.pressure_bar < 4.3


@pytest.mark.verifies("REQ-009")
def test_high_pressure_warning_is_raised_before_the_trip(make_bench):
    bench = start_with_failed_expansion_vessel(make_bench)

    assert bench.run_until(lambda b: b.raised(Fault.PRESSURE_HIGH), 600)

    assert not bench.raised(Fault.OVER_PRESSURE)


@pytest.mark.verifies("REQ-010")
def test_leak_trips_on_low_pressure(running_bench):
    running_bench.inject("LEAK")

    assert running_bench.run_until(lambda b: b.raised(Fault.LOW_PRESSURE), 300)
    assert running_bench.state is State.FAULT
    assert running_bench.status.heater_contactor == 0


@pytest.mark.verifies("REQ-006")
def test_biased_temperature_sensor_trips_at_the_sensed_limit(running_bench):
    running_bench.inject("TEMP_SENSOR_OFFSET")
    running_bench.command(set_setpoint=1, setpoint_c=90.0)

    running_bench.run(1500)

    assert not running_bench.raised(Fault.OVER_TEMP)
    assert running_bench.plant.temperature_c == pytest.approx(82.0, abs=2.0)


@pytest.mark.verifies("REQ-015")
def test_fault_can_be_reset_after_the_cause_is_removed(running_bench):
    running_bench.inject("PUMP_TRIPPED")
    running_bench.run_until(lambda b: b.state is State.FAULT, 10)
    running_bench.clear()

    running_bench.run(5)
    assert running_bench.state is State.FAULT
    running_bench.command(reset=1)
    running_bench.run(1)

    assert running_bench.state is State.STANDBY


@pytest.mark.verifies("REQ-015")
def test_reset_is_refused_while_the_cause_remains(running_bench):
    running_bench.inject("LEAK")
    running_bench.run_until(lambda b: b.state is State.FAULT, 300)

    running_bench.command(ack=1, reset=1)
    running_bench.run(2)

    assert running_bench.state is State.FAULT


@pytest.mark.verifies("REQ-015")
def test_boiler_restarts_after_a_reset(running_bench):
    running_bench.inject("LEAK")
    running_bench.run_until(lambda b: b.state is State.FAULT, 300)
    running_bench.clear()
    running_bench.plant.fill_pressure_bar = 1.2
    running_bench.run(30)
    running_bench.command(reset=1)
    running_bench.run(2)
    assert running_bench.state is State.STANDBY

    running_bench.command(start=1)

    assert running_bench.run_until(lambda b: b.state is State.RUNNING, 60)


@pytest.mark.verifies("REQ-040")
def test_fault_injection_is_reversible(running_bench):
    running_bench.inject("FLOW_SENSOR_OPEN")
    running_bench.clear("FLOW_SENSOR_OPEN")

    running_bench.run(30)

    assert running_bench.state is State.RUNNING
    assert not running_bench.raised(Fault.FLOW_SENSOR)


@pytest.mark.verifies("REQ-041")
def test_identical_runs_produce_identical_traces():
    def trace():
        bench = BoilerBench(BoilerParams(noise=True, seed=11, initial_temperature_c=60.0))
        try:
            bench.run(2)
            bench.command(start=1)
            bench.run(120)
            bench.inject("PUMP_SEIZED")
            bench.run(30)
            return bench.recorder.rows
        finally:
            bench.close()

    assert trace() == trace()
