"""REQ-014 flight-history tests using only the public closed-loop bench API."""

import pytest
from landing_gear.bench import STEP_MS
from landing_gear.model import Fault, default_config


def _assert_warning(bench, expected):
    assert bench.raised(Fault.TOO_LOW_GEAR) == expected
    assert bench.outputs["gear_horn"] == int(expected)


def _fly_feet(bench, feet):
    """Move one foot per plant step, then sample the final altitude at rest.

    The controller samples before the plant advances, so the final stationary
    step is necessary to evaluate the altitude reached by the dynamics.
    """
    bench.command(vertical_speed_fpm=1200 if feet > 0 else -1200)
    for _ in range(abs(feet)):
        bench.step()
    bench.command(vertical_speed_fpm=0)
    bench.step()


@pytest.mark.verifies("REQ-014")
@pytest.mark.parametrize("offset", [-1, 0, 1])
def test_dynamic_climb_apex_controls_later_approach_warning(make_bench, offset):
    config = default_config()
    limit = config.gear_warning_altitude_ft
    bench = make_bench(
        initial_altitude_ft=limit - 2,
        initial_airspeed_kt=config.gear_warning_airspeed_kt - 1,
        gear_down=False,
    )

    # Reach an apex just below, at, or just above the arming threshold.
    _fly_feet(bench, offset + 2)
    assert bench.plant.altitude_ft == limit + offset
    _assert_warning(bench, False)

    # Descend to the same low altitude in every case. A missed initial climb
    # must not arm, whereas even a single sampled equality must qualify.
    _fly_feet(bench, -(offset + 2))
    assert bench.plant.altitude_ft == limit - 2
    _assert_warning(bench, offset >= 0)

    if offset < 0:
        # Demonstrate a positive warning after a subsequent qualifying climb,
        # rather than passing solely because this bench never sounds its horn.
        _fly_feet(bench, 2)
        _assert_warning(bench, False)
        _fly_feet(bench, -1)
        _assert_warning(bench, True)


@pytest.mark.verifies("REQ-014")
def test_failed_altimeter_climb_does_not_qualify_but_later_failure_retains_history(make_bench):
    config = default_config()
    limit = config.gear_warning_altitude_ft
    bench = make_bench(
        initial_altitude_ft=limit - 1,
        initial_airspeed_kt=config.gear_warning_airspeed_kt - 1,
        gear_down=False,
    )

    # The physical aircraft crosses the threshold, but unavailable radio
    # altitude must not count as a qualifying measurement.
    bench.inject("RADIO_ALTIMETER_FAILED")
    _fly_feet(bench, 2)
    assert bench.plant.altitude_ft == limit + 1
    _assert_warning(bench, False)
    _fly_feet(bench, -2)
    _assert_warning(bench, False)
    bench.clear("RADIO_ALTIMETER_FAILED")
    bench.step()
    assert bench.plant.altitude_ft == limit - 1
    _assert_warning(bench, False)

    # A new, valid climb qualifies. Losing validity during the ensuing descent
    # suppresses evaluation, but must not erase the newly established history.
    _fly_feet(bench, 1)
    assert bench.plant.altitude_ft == limit
    _assert_warning(bench, False)
    bench.inject("RADIO_ALTIMETER_FAILED")
    _fly_feet(bench, -1)
    _assert_warning(bench, False)
    bench.clear("RADIO_ALTIMETER_FAILED")
    bench.step()
    assert bench.plant.altitude_ft == limit - 1
    _assert_warning(bench, True)


@pytest.mark.verifies("REQ-014")
def test_touchdown_clears_history_before_a_second_low_altitude_takeoff(make_bench):
    config = default_config()
    limit = config.gear_warning_altitude_ft
    bench = make_bench(
        initial_airspeed_kt=120,
        rotate_kt=120,
        climb_fpm=1200,
    )

    # Rotation occurs after the first ground sample. Sample airborne once more
    # before asking for retraction, so the plant's lever lock has released.
    bench.step()
    bench.step()
    assert not bench.plant.on_ground
    assert bench.outputs["handle_lock"] == 0
    bench.command(gear_handle_down=0)
    while bench.plant.altitude_ft < limit:
        bench.step()
        _assert_warning(bench, False)
    assert bench.lights() == "- - -"
    assert bench.plant.altitude_ft == limit

    # Explicitly sample the attained threshold, then descend with gear up.
    bench.command(vertical_speed_fpm=0)
    bench.step()
    _assert_warning(bench, False)
    _fly_feet(bench, -1)
    _assert_warning(bench, True)

    # Complete a real hydraulic extension before landing; only downlocked
    # main legs on the ground produce the plant's WOW indications.
    bench.command(gear_handle_down=1)
    assert bench.run_until(lambda b: b.lights() == "G G G", timeout_s=10)
    bench.step()
    _assert_warning(bench, False)
    bench.command(vertical_speed_fpm=-1200)
    assert bench.run_until(lambda b: b.plant.on_ground, timeout_s=limit / 20 + 1)
    bench.step()
    assert bench.outputs["handle_lock"] == 1
    _assert_warning(bench, False)

    # A second departure must not inherit the first flight's qualification.
    # Climb control permits departure even though automatic rotation is one-shot.
    bench.command(vertical_speed_fpm=1200)
    bench.step()
    bench.step()
    assert not bench.plant.on_ground
    assert bench.outputs["handle_lock"] == 0
    bench.command(gear_handle_down=0)
    for _ in range(round(10_000 / STEP_MS)):
        bench.step()
        assert bench.plant.altitude_ft < limit
        _assert_warning(bench, False)
        if bench.lights() == "- - -":
            break
    assert bench.lights() == "- - -"
