import pytest
from smart_boiler.bench import BoilerBench
from smart_boiler.plant import BoilerParams


@pytest.fixture
def make_bench():
    """Factory for benches; every bench is closed at the end of the test."""
    benches = []

    def factory(**params):
        params.setdefault("noise", False)
        bench = BoilerBench(BoilerParams(**params))
        benches.append(bench)
        return bench

    yield factory
    for bench in benches:
        bench.close()


@pytest.fixture
def bench(make_bench):
    return make_bench()


@pytest.fixture
def running_bench(make_bench):
    """A boiler that has finished startup and is regulating at 80 degC."""
    bench = make_bench(initial_temperature_c=78.0)
    bench.run(2)
    bench.command(start=1)
    assert bench.run_until(lambda b: b.state.name == "RUNNING", 30)
    return bench
