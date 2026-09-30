import csv

from sparrow.ai.analysis import summarize_trace

STATES = ["INIT", "STANDBY", "RUNNING", "FAULT"]
FAULTS = ["A", "B", "C"]


def write_trace(path, rows):
    with path.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def row(t, state, temperature, active=0, latched=0):
    return {
        "time_s": t,
        "state": state,
        "temperature_c": temperature,
        "alarms_active": active,
        "alarms_latched": latched,
    }


def test_summary_lists_state_changes_and_alarm_edges(tmp_path):
    path = tmp_path / "trace.csv"
    write_trace(
        path,
        [
            row(0, 0, 20),
            row(1, 2, 40),
            row(2, 2, 90, active=0b010),
            row(3, 3, 111, active=0b110, latched=0b100),
            row(4, 3, 100, latched=0b100),
        ],
    )

    summary = summarize_trace(path, STATES, FAULTS)

    assert "5 samples" in summary
    assert "temperature_c: 20.000 / 111.000 / 100.000" in summary
    assert "1.0 s  state INIT -> RUNNING" in summary
    assert "2.0 s  alarms_active + B" in summary
    assert "3.0 s  alarms_latched + C" in summary
    assert "4.0 s  alarms_active - B" in summary


def test_recorded_boiler_trace_can_be_summarized(tmp_path):
    from smart_boiler.bench import BoilerBench
    from smart_boiler.model import FAULT_INFO, STATE_LABELS
    from smart_boiler.plant import BoilerParams

    bench = BoilerBench(BoilerParams(noise=False, initial_temperature_c=78.0))
    try:
        bench.run(2)
        bench.command(start=1)
        bench.run(20)
        bench.inject("PUMP_SEIZED")
        bench.run(15)
        bench.recorder.write_csv(tmp_path / "run.csv")
    finally:
        bench.close()

    summary = summarize_trace(
        tmp_path / "run.csv", [s.name for s in STATE_LABELS], [f.name for f in FAULT_INFO]
    )

    assert "STARTUP -> RUNNING" in summary
    assert "alarms_latched + NO_FLOW" in summary
    assert "RUNNING -> FAULT" in summary


def test_state_changes_without_a_temperature_column_carry_no_detail(tmp_path):
    path = tmp_path / "trace.csv"
    write_trace(path, [{"time_s": 0, "state": 0}, {"time_s": 1, "state": 2}])

    summary = summarize_trace(path, STATES, FAULTS)

    assert "1.0 s  state INIT -> RUNNING\n" in summary + "\n"
    assert "nan" not in summary
