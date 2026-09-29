# Testing

```sh
make test          # C unit tests, pytest (closed loop, SIL, tools), traceability with results
make test-asan     # C tests under AddressSanitizer and UBSan
make lint          # ruff, clang-format, cppcheck, stale generated code, stale traceability
```

## Layers

| Layer | Location | Runs | What it proves |
|---|---|---|---|
| Core unit tests | `tests/core` | CTest | alarms, timers, PI, history ring, frame codec |
| Controller unit tests | `examples/smart-boiler/tests/c` | CTest | every limit, delay, transition and interlock, with synthetic inputs |
| HMI model tests | `tests/c/test_hmi_model.c` | CTest | severity mapping, history, notifications, control enablement, link loss |
| Plant tests | `tests/python/test_plant.py` | pytest | model physics and every plant fault |
| Closed loop | `tests/python/test_closed_loop.py` | pytest | the C controller against the plant: regulation, startup, shutdown, each fault end to end |
| Software in the loop | `tests/python/test_runtime_sil.py` | pytest | the `boiler_runtime` binary over UDP: timing, lost I/O, safe shutdown, watchdog, command merging |
| Tooling | `tests/python` | pytest | code generation, traceability, AI layer (stub server, no network) |
| Cross target | preset `target-aarch64-test` | CTest + qemu | the C tests as aarch64 binaries |

The C tests use a small harness (`sparrow/testing/sp_test.h`, [ADR 4](adr/0004-minimal-c-test-harness.md)). `tests/c/boiler_fixture.h` drives a controller with engineering-unit inputs, converted to loop currents the way a transmitter does, and can emulate a cooperative plant. The Python closed-loop tests use `smart_boiler.bench.BoilerBench`, which steps the C library and the plant at a fixed 100 ms, so every run is deterministic. `test_identical_runs_produce_identical_traces` checks that, noise included.

## Boundaries

Every limit is tested at the limit, and on both sides of it at the transmitter resolution. For the 110.0 °C trip:

```c
SP_TEST(over_temperature_does_not_trip_below_the_limit, "REQ-006")   /* 109.9 */
SP_TEST(over_temperature_does_not_trip_at_the_limit, "REQ-006")      /* 110.0 */
SP_TEST(over_temperature_trips_above_the_limit, "REQ-006")           /* 110.1 */
```

Comparisons are exact because the controller rounds readings to the transmitter resolution before comparing (REQ-001). Delays are tested just before and just after they expire, and hysteresis just inside and at the release point.

## Requirement references

Each test names the requirements it verifies:

```c
SP_TEST(low_pressure_trips_after_two_seconds, "REQ-010") { ... }
```

```python
@pytest.mark.verifies("REQ-010")
def test_leak_trips_on_low_pressure(running_bench): ...
```

`make test` writes JUnit files to `build/<preset>/results/` and a traceability report with pass/fail per requirement to `build/<preset>/traceability.md`. A test without a reference is allowed, for example a core library test that no requirement covers directly. A reference to an unknown ID fails `sparrow trace`.

## Sanitizers and static analysis

`make test-asan` builds with `-fsanitize=address,undefined` and runs the C tests. It runs them with ASLR disabled, because GCC 12 and 13 sanitizer runtimes crash at random on kernels with high mmap randomization. The Python suites are not run under ASan: Python cannot load an instrumented library without preloading the runtime.

`make lint` runs cppcheck with `--enable=warning,portability` over the core, controller, runtime and generated code. All C code builds with `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wstrict-prototypes -Wmissing-prototypes` and has no warnings.

## Screenshots

The images in `docs/assets` are produced by the application, not drawn. `scripts/capture-screenshots.py` runs each scenario in the simulator, holds it at a fixed simulation time and captures the headless HMI. The same script records the README animation. Rerun it after any change to the HMI or the plant.
