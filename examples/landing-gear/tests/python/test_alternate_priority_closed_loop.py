import pytest
from landing_gear.bench import STEP_MS
from landing_gear.model import Fault, State, default_config
from landing_gear.plant import GearParams

PERIOD_S = STEP_MS / 1000.0
ACTUATION_MIN_PSI = GearParams().actuation_min_psi
GEAR_DISAGREE_MASK = 1 << int(Fault.GEAR_DISAGREE)


def _assert_alternate(bench, *, faulted=False):
    assert (
        bench.outputs["down_valve"],
        bench.outputs["up_valve"],
        bench.outputs["uplock_release"],
    ) == (0, 0, 1)
    assert (bench.status.alarms_active & GEAR_DISAGREE_MASK) == 0
    assert bool(bench.status.alarms_latched & GEAR_DISAGREE_MASK) == faulted
    assert bench.outputs["master_warning"] == int(faulted)
    assert bench.state == (State.FAULT if faulted else State.ALTERNATE_EXTENDING)


@pytest.mark.verifies("REQ-009")
@pytest.mark.parametrize(
    "pressure_psi",
    [
        0.0,
        ACTUATION_MIN_PSI - 1.0,
        ACTUATION_MIN_PSI,
        ACTUATION_MIN_PSI + 1.0,
        GearParams().nominal_psi,
    ],
)
def test_slow_freefall_ignores_pressure_then_release_resumes_retraction(make_bench, pressure_psi):
    timeout_ms = default_config().normal_transit_timeout_ms
    timeout_s = timeout_ms / 1000.0
    bench = make_bench(
        initial_altitude_ft=3000.0,
        initial_airspeed_kt=220.0,
        gear_down=False,
        nominal_psi=pressure_psi,
        freefall_time_s=timeout_s + 1.0,
    )
    bench.step()
    assert bench.state == State.UP_LOCKED

    # Keep the normal lever up throughout a free-fall longer than its hydraulic timeout.
    started_ms = bench.time_ms
    bench.command(alternate_extend=1)
    incomplete_beyond_timeout = False
    for _ in range(round((bench.params.freefall_time_s + 1.0) / PERIOD_S)):
        bench.step()
        _assert_alternate(bench)
        complete = all(leg.downlocked for leg in bench.plant.legs.values())
        if bench.time_ms - started_ms > timeout_ms and not complete:
            incomplete_beyond_timeout = True
        if complete:
            break
    else:
        pytest.fail("alternate extension did not reach all three downlocks")

    assert incomplete_beyond_timeout
    assert bench.lights() == "G G G"
    bench.step()  # Feed completed lock indications back while the handle remains pulled.
    _assert_alternate(bench)

    positions = {name: leg.position for name, leg in bench.plant.legs.items()}
    bench.command(alternate_extend=0)
    bench.step()
    assert (
        bench.outputs["down_valve"],
        bench.outputs["up_valve"],
        bench.outputs["uplock_release"],
    ) == (0, 1, 0)
    assert not bench.raised(Fault.GEAR_DISAGREE)

    powered = pressure_psi >= bench.params.actuation_min_psi
    for name, leg in bench.plant.legs.items():
        if powered:
            assert leg.position < positions[name]
            assert not leg.downlocked
        else:
            assert leg.position == positions[name]
            assert leg.downlocked

    assert bench.run_until(
        lambda b: b.state in (State.UP_LOCKED, State.FAULT),
        timeout_s + 2.0 * PERIOD_S,
    )
    if powered:
        # At and above the plant pressure boundary, normal retraction completes.
        assert bench.state == State.UP_LOCKED
        assert all(leg.uplocked for leg in bench.plant.legs.values())
        assert not bench.raised(Fault.GEAR_DISAGREE)
        assert bench.outputs["master_warning"] == 0
        assert bench.outputs["up_valve"] == 0
    else:
        # Below the boundary, normal drive fails and must actually latch the alarm.
        assert bench.state == State.FAULT
        assert (bench.status.alarms_active & GEAR_DISAGREE_MASK) != 0
        assert (bench.status.alarms_latched & GEAR_DISAGREE_MASK) != 0
        assert bench.outputs["master_warning"] == 1
        assert bench.outputs["up_valve"] == 1

        # A new pull still takes priority; sensed agreement is not a fault reset.
        bench.command(alternate_extend=1, gear_handle_down=1)
        bench.step()
        _assert_alternate(bench, faulted=True)
        assert all(leg.downlocked for leg in bench.plant.legs.values())
        bench.command(alternate_extend=0)
        bench.step()
        assert bench.outputs["uplock_release"] == 0
        assert bench.outputs["down_valve"] == bench.outputs["up_valve"] == 0
        assert bench.state == State.FAULT
        assert (bench.status.alarms_latched & GEAR_DISAGREE_MASK) != 0
        assert bench.outputs["master_warning"] == 1


@pytest.mark.verifies("REQ-009")
@pytest.mark.parametrize("source_down", [False, True])
def test_alternate_replaces_active_hydraulic_motion_and_ignores_live_lever(make_bench, source_down):
    bench = make_bench(
        initial_altitude_ft=3000.0,
        initial_airspeed_kt=220.0,
        gear_down=not source_down,
    )
    bench.command(gear_handle_down=int(source_down))
    bench.run(1.0)
    assert bench.outputs["down_valve"] == int(source_down)
    assert bench.outputs["up_valve"] == int(not source_down)
    positions = {name: leg.position for name, leg in bench.plant.legs.items()}
    assert all(0.0 < position < 1.0 for position in positions.values())

    bench.command(alternate_extend=1)
    bench.step()
    _assert_alternate(bench)
    for name, leg in bench.plant.legs.items():
        # Retraction reverses immediately; powered extension changes to free-fall rate.
        assert leg.position == pytest.approx(
            positions[name] + PERIOD_S / bench.params.freefall_time_s
        )

    for step in range(round(bench.params.freefall_time_s / PERIOD_S) + 2):
        positions = {name: leg.position for name, leg in bench.plant.legs.items()}
        bench.command(gear_handle_down=step % 2)
        bench.step()
        _assert_alternate(bench)
        assert bench.outputs["handle_lock"] == 0
        assert all(leg.position >= positions[name] for name, leg in bench.plant.legs.items())
        if all(leg.downlocked for leg in bench.plant.legs.values()):
            break
    else:
        pytest.fail("live lever changes prevented alternate-extension completion")

    # Completion with the lever up must not withdraw the alternate release command.
    bench.command(gear_handle_down=0)
    bench.step()
    _assert_alternate(bench)
    assert bench.lights() == "G G G"

    bench.command(gear_handle_down=1, alternate_extend=0)
    bench.step()
    assert bench.state == State.DOWN_LOCKED
    assert bench.outputs["uplock_release"] == 0
    assert bench.outputs["down_valve"] == bench.outputs["up_valve"] == 0
    assert not bench.raised(Fault.GEAR_DISAGREE)


@pytest.mark.verifies("REQ-009")
def test_alternate_handle_does_not_unlock_ground_lever(ground_bench):
    bench = ground_bench
    bench.step()  # Apply the controller's lever-lock command to the physical plant.
    assert bench.plant.handle_locked

    for alternate in (1, 1, 0):
        bench.command(alternate_extend=alternate, gear_handle_down=0)
        assert bench.plant.lever_blocked
        bench.step()
        assert bench.plant.gear_handle_down
        assert bench.plant.handle_locked
        assert bench.outputs["handle_lock"] == 1
        assert bench.outputs["down_valve"] == bench.outputs["up_valve"] == 0
        assert bench.outputs["uplock_release"] == alternate
        assert bench.lights() == "G G G"
        assert not bench.raised(Fault.GEAR_DISAGREE)
        if alternate:
            _assert_alternate(bench)
        else:
            assert bench.state == State.DOWN_LOCKED
