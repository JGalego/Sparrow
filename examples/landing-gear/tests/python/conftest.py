import pytest
from landing_gear.bench import GearBench
from landing_gear.plant import GearParams


@pytest.fixture
def make_bench():
    """Factory for benches; every bench is closed at the end of the test.

    Keyword arguments are GearParams fields, for example
    make_bench(initial_altitude_ft=1500, initial_airspeed_kt=160, gear_down=False).
    """
    benches = []

    def factory(**params):
        bench = GearBench(GearParams(**params))
        benches.append(bench)
        return bench

    yield factory
    for bench in benches:
        bench.close()


@pytest.fixture
def ground_bench(make_bench):
    """Parked on the runway: gear down and locked, lever down, 3000 psi."""
    return make_bench()


@pytest.fixture
def cruise_bench(make_bench):
    """Level at 3000 ft and 220 kt with the gear up and locked and the lever up."""
    bench = make_bench(initial_altitude_ft=3000.0, initial_airspeed_kt=220.0, gear_down=False)
    return bench
