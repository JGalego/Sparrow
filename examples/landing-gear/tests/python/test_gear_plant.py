"""Plant model on its own, without the controller."""

import pytest
from landing_gear.plant import GearParams, GearPlant

from sparrow.simulation.plant import UnknownFault

DT = 0.05


def run(plant, seconds, **actuators):
    for _ in range(round(seconds / DT)):
        plant.step(DT, actuators)


def airborne(**params):
    return GearPlant(GearParams(initial_altitude_ft=3000.0, initial_airspeed_kt=200.0, **params))


def test_gear_extends_under_hydraulic_power_in_the_extension_time():
    plant = airborne(gear_down=False)

    run(plant, 6.9, down_valve=1)
    assert not plant.legs["nose"].downlocked
    run(plant, 0.2, down_valve=1)

    assert all(leg.downlocked for leg in plant.legs.values())
    assert plant.sensors()["left_downlock"] == 1
    assert plant.sensors()["left_uplock"] == 0


def test_gear_retracts_and_uplocks_in_the_retraction_time():
    plant = airborne()

    run(plant, 8.1, up_valve=1)

    assert all(leg.uplocked for leg in plant.legs.values())
    assert plant.sensors()["nose_uplock"] == 1


def test_uplocks_hold_the_gear_without_hydraulic_pressure():
    plant = airborne(gear_down=False)
    plant.inject("HYD_PUMP_FAILED")
    run(plant, 10.0)

    run(plant, 30.0, down_valve=1)

    assert all(leg.uplocked for leg in plant.legs.values())
    assert plant.pressure_psi < 10.0


def test_released_gear_free_falls_to_down_and_locked():
    plant = airborne(gear_down=False)
    plant.inject("HYD_PUMP_FAILED")
    run(plant, 10.0)

    run(plant, 11.9, uplock_release=1)
    assert not plant.legs["left"].downlocked
    run(plant, 0.2, uplock_release=1)

    assert all(leg.downlocked for leg in plant.legs.values())


def test_a_jammed_nose_gear_does_not_move():
    plant = airborne(gear_down=False)
    plant.inject("NOSE_GEAR_JAMMED")

    run(plant, 10.0, down_valve=1)

    assert plant.legs["nose"].uplocked
    assert plant.legs["left"].downlocked


def test_squat_switches_are_made_only_on_the_ground_with_the_gear_down():
    parked = GearPlant(GearParams())
    flying = airborne()

    assert parked.sensors()["left_wow"] == parked.sensors()["right_wow"] == 1
    assert flying.sensors()["left_wow"] == 0

    parked.inject("WOW_STUCK_AIR")
    assert parked.sensors()["right_wow"] == 0


def test_radio_altimeter_is_invalid_above_its_range_or_when_failed():
    plant = airborne()
    low = GearPlant(GearParams(initial_altitude_ft=800.0, initial_airspeed_kt=150.0))

    assert plant.sensors()["radio_altitude_valid"] == 0
    assert low.sensors()["radio_altitude_valid"] == 1
    assert low.sensors()["radio_altitude_ft"] == 800.0

    low.inject("RADIO_ALTIMETER_FAILED")
    assert low.sensors()["radio_altitude_valid"] == 0


def test_lever_lock_holds_the_lever_down():
    plant = GearPlant(GearParams())
    plant.step(DT, {"handle_lock": 1})

    plant.control("gear_handle_down", 0)

    assert plant.gear_handle_down
    assert plant.lever_blocked


def test_aircraft_lifts_off_at_rotation_speed_and_records_touchdowns():
    plant = GearPlant(
        GearParams(initial_airspeed_kt=130.0, acceleration_kt_per_s=2.0, rotate_kt=140.0)
    )

    run(plant, 4.9)
    assert plant.on_ground
    run(plant, 1.0)
    assert not plant.on_ground and plant.altitude_ft > 0

    plant.control("vertical_speed_fpm", -3000.0)
    run(plant, 5.0)
    assert plant.touchdowns == [True]


def test_unknown_faults_and_controls_are_rejected():
    plant = GearPlant()

    with pytest.raises(UnknownFault):
        plant.inject("ENGINE_FIRE")
    with pytest.raises(UnknownFault):
        plant.control("flaps", 30)
