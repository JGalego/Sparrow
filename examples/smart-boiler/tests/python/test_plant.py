import pytest
from smart_boiler.model import PlantFault
from smart_boiler.plant import BoilerParams, BoilerPlant
from sparrow.simulation.plant import UnknownFault

IDLE = {"heater_power_pct": 0.0, "heater_contactor": 0, "pump_run": 0, "valve_open": 0}


def run(plant, seconds, **actuators):
    commands = {**IDLE, **actuators}
    for _ in range(round(seconds * 10)):
        plant.step(0.1, commands)


def ma_to_value(ma, low, high):
    return low + (ma - 4.0) / 16.0 * (high - low)


@pytest.fixture
def plant():
    return BoilerPlant(BoilerParams(noise=False))


def test_idle_boiler_cools_towards_the_ambient(plant):
    plant.temperature_c = 80.0

    run(plant, 600)

    assert 20.0 < plant.temperature_c < 80.0


def test_heater_heats_at_the_rate_given_by_power_and_capacity(plant):
    run(plant, 60, heater_power_pct=100.0, heater_contactor=1)

    expected_rise = 15_000 * 60 / 125_000
    assert plant.temperature_c - 20.0 == pytest.approx(expected_rise, rel=0.02)


def test_open_contactor_removes_all_heat_even_at_full_power_request(plant):
    run(plant, 60, heater_power_pct=100.0, heater_contactor=0)

    assert plant.temperature_c == pytest.approx(20.0)


def test_shorted_heater_relay_ignores_the_power_request(plant):
    plant.inject("HEATER_SSR_SHORTED")

    run(plant, 60, heater_power_pct=0.0, heater_contactor=1)

    assert plant.temperature_c > 25.0


def test_shorted_heater_relay_is_cut_by_the_contactor(plant):
    plant.inject("HEATER_SSR_SHORTED")

    run(plant, 60, heater_power_pct=0.0, heater_contactor=0)

    assert plant.temperature_c == pytest.approx(20.0)


def test_valve_travels_at_a_limited_speed(plant):
    run(plant, 4, valve_open=1)
    assert plant.valve_position_pct == pytest.approx(50.0, abs=0.5)

    run(plant, 5, valve_open=1)
    assert plant.valve_position_pct == 100.0


def test_stuck_valve_does_not_move(plant):
    run(plant, 2, valve_open=1)
    position = plant.valve_position_pct
    plant.inject("VALVE_STUCK")

    run(plant, 10, valve_open=1)

    assert plant.valve_position_pct == position


def test_flow_needs_pump_and_open_valve(plant):
    run(plant, 12, pump_run=1, valve_open=0)
    assert plant.flow_lpm == 0.0

    run(plant, 12, pump_run=1, valve_open=1)
    assert plant.flow_lpm == pytest.approx(40.0, rel=0.02)


def test_seized_pump_reports_running_but_delivers_no_flow(plant):
    plant.inject("PUMP_SEIZED")

    run(plant, 12, pump_run=1, valve_open=1)

    assert plant.pump_running_feedback
    assert plant.flow_lpm == 0.0


def test_tripped_pump_reports_stopped(plant):
    plant.inject("PUMP_TRIPPED")

    run(plant, 12, pump_run=1, valve_open=1)

    assert not plant.pump_running_feedback
    assert plant.flow_lpm == 0.0


def test_pump_against_a_closed_valve_raises_pressure(plant):
    run(plant, 10, pump_run=1, valve_open=0)

    assert plant.pressure_bar > 4.0


def test_failed_expansion_vessel_multiplies_the_thermal_pressure_rise(plant):
    plant.temperature_c = 70.0
    healthy = plant.pressure_bar

    plant.inject("EXPANSION_VESSEL_FAILED")

    assert plant.pressure_bar - 1.2 == pytest.approx(4 * (healthy - 1.2))


def test_leak_drains_the_fill_pressure_to_zero(plant):
    plant.inject("LEAK")

    run(plant, 120)

    assert plant.pressure_bar == 0.0


@pytest.mark.parametrize(
    ("fault", "sensor", "expected_ma"),
    [
        ("TEMP_SENSOR_OPEN", "temperature_ma", 0.0),
        ("TEMP_SENSOR_SHORT", "temperature_ma", 22.0),
        ("PRESSURE_SENSOR_OPEN", "pressure_ma", 0.0),
        ("FLOW_SENSOR_OPEN", "flow_ma", 0.0),
        ("VALVE_SENSOR_OPEN", "valve_position_ma", 0.0),
    ],
)
def test_sensor_faults_drive_the_loop_current_out_of_range(plant, fault, sensor, expected_ma):
    plant.inject(fault)

    assert plant.sensors()[sensor] == expected_ma


def test_temperature_offset_fault_reads_high(plant):
    plant.temperature_c = 50.0
    plant.inject("TEMP_SENSOR_OFFSET")

    reading = ma_to_value(plant.sensors()["temperature_ma"], 0.0, 150.0)

    assert reading == pytest.approx(58.0, abs=1e-3)


def test_healthy_sensors_report_the_process_values(plant):
    plant.temperature_c = 75.0

    sensors = plant.sensors()

    assert sensors["temperature_ma"] == pytest.approx(12.0)
    assert sensors["pressure_ma"] == pytest.approx(4.0 + 16.0 * plant.pressure_bar / 6.0)


def test_clear_removes_one_fault_or_all(plant):
    plant.inject("LEAK")
    plant.inject("VALVE_STUCK")

    plant.clear("LEAK")
    assert plant.active_faults() == {"VALVE_STUCK"}

    plant.clear()
    assert plant.active_faults() == frozenset()


def test_unknown_fault_name_is_rejected(plant):
    with pytest.raises(UnknownFault):
        plant.inject("NOT_A_FAULT")


def test_every_modelled_fault_can_be_injected(plant):
    for fault in PlantFault:
        if fault is not PlantFault.NONE:
            plant.inject(fault.name)


def test_same_seed_gives_identical_noise():
    first = BoilerPlant(BoilerParams(seed=7))
    second = BoilerPlant(BoilerParams(seed=7))

    assert [first.sensors()["temperature_ma"] for _ in range(20)] == [
        second.sensors()["temperature_ma"] for _ in range(20)
    ]


def test_different_seeds_give_different_noise():
    first = BoilerPlant(BoilerParams(seed=1))
    second = BoilerPlant(BoilerParams(seed=2))

    assert [first.sensors()["temperature_ma"] for _ in range(5)] != [
        second.sensors()["temperature_ma"] for _ in range(5)
    ]
