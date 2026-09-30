"""Scenario runner: plays a scenario against the C controller and prints a cockpit event log.

The run is not paced in real time; a two-minute scenario takes well under a
second. Every line is something the crew would see or hear, or something the
scenario did.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import yaml

from sparrow.simulation.scenario import ScenarioPlayer, load_scenario

from .bench import GearBench
from .model import FAULT_INFO, PLANT_FAULT_LABELS, STATE_LABELS, Fault, PlantFault, State
from .plant import GearParams

SCENARIO_DIR = Path(__file__).resolve().parents[1] / "scenarios"
DEFAULT_DURATION_S = 120.0
PILOT_WORDS = {
    "gear_handle_down": lambda v: f"gear lever {'DOWN' if v else 'UP'}",
    "alternate_extend": lambda v: f"alternate extension handle {'PULLED' if v else 'stowed'}",
    "mute": lambda v: "MUTE pressed",
    "reset": lambda v: "RESET pressed",
    "vertical_speed_fpm": lambda v: f"vertical speed {v:+.0f} fpm",
    "acceleration_kt_per_s": lambda v: (
        f"{'accelerating' if v >= 0 else 'decelerating'} {abs(v):g} kt/s"
    ),
}


class LoggingBench(GearBench):
    """A bench that reports what the scenario does to it."""

    def __post_init__(self) -> None:
        super().__post_init__()
        self.notes: list[str] = []

    def inject(self, fault: str) -> None:
        super().inject(fault)
        self.notes.append(f"FAILURE: {PLANT_FAULT_LABELS[PlantFault[fault]]}")

    def clear(self, fault: str | None = None) -> None:
        super().clear(fault)
        self.notes.append("failures cleared" if fault is None else f"cleared: {fault}")

    def command(self, **fields: float) -> None:
        super().command(**fields)
        for name, value in fields.items():
            words = PILOT_WORDS[name](value)
            if name == "gear_handle_down" and self.plant.lever_blocked:
                words += " -- lever lock holds it down"
            self.notes.append(f"pilot: {words}")


def resolve_scenario(name: str) -> Path:
    path = Path(name)
    return path if path.exists() else SCENARIO_DIR / f"{name}.yaml"


def _alarm_text(bit: int) -> tuple[str, bool]:
    try:
        info = FAULT_INFO[Fault(bit)]
    except ValueError:
        return f"alarm bit {bit}", False
    return info.text, info.critical


def _changes(before: dict, after: dict, bench: GearBench) -> list[str]:
    events = []
    if after["on_ground"] != before["on_ground"]:
        if after["on_ground"]:
            locked = bench.plant.touchdowns[-1]
            events.append(
                "touchdown, gear down and locked" if locked else "TOUCHDOWN WITH GEAR NOT LOCKED"
            )
        else:
            events.append("liftoff")
    if after["state"] != before["state"]:
        events.append(f"controller: {STATE_LABELS[State(after['state'])]}")
    if after["lights"] != before["lights"]:
        suffix = "  three green" if after["lights"] == "G G G" else ""
        events.append(f"gear lights {after['lights']}{suffix}")
    for bit in range(32):
        was, now = before["alarms"] >> bit & 1, after["alarms"] >> bit & 1
        if was != now:
            text, critical = _alarm_text(bit)
            if now:
                events.append(f"{'WARNING' if critical else 'caution'}: {text}")
            else:
                events.append(f"clears: {text}")
    for name, label in (("gear_horn", "gear horn"), ("master_warning", "master warning")):
        if after[name] != before[name]:
            events.append(f"{label} {'ON' if after[name] else 'off'}")
    return events


def _snapshot(bench: GearBench) -> dict:
    return {
        "state": bench.status.state,
        "alarms": bench.status.alarms_active | bench.status.alarms_latched,
        "lights": bench.lights(),
        "on_ground": bench.plant.on_ground,
        "gear_horn": bench.outputs["gear_horn"],
        "master_warning": bench.outputs["master_warning"],
    }


def _line(bench: GearBench, event: str) -> str:
    plant = bench.plant
    state = STATE_LABELS[bench.state]
    return (
        f"{bench.time_s:6.2f}  {plant.altitude_ft:5.0f}  {plant.airspeed_kt:4.0f}  "
        f"{plant.pressure_psi:5.0f}  {bench.lights()}  {state:<15} {event}"
    )


def run(args: argparse.Namespace) -> int:
    path = resolve_scenario(args.scenario)
    scenario = load_scenario(path)
    duration = args.stop_at_s or yaml.safe_load(path.read_text("utf-8")).get(
        "duration_s", DEFAULT_DURATION_S
    )
    bench = LoggingBench(GearParams(**scenario.plant))
    player = ScenarioPlayer(bench, scenario)
    print(f"scenario {scenario.name}: {scenario.description}\n")
    print("  time    alt  kias    hyd  N L R  controller      event")
    before = _snapshot(bench)
    print(_line(bench, "start"))
    try:
        while bench.time_s < duration:
            player.apply_due_events()
            for note in bench.notes:
                print(_line(bench, note))
            bench.notes.clear()
            bench.step()
            after = _snapshot(bench)
            for event in _changes(before, after, bench):
                print(_line(bench, event))
            before = after
        alarms = [_alarm_text(b)[0] for b in range(32) if before["alarms"] >> b & 1]
        print(
            f"\nend at {bench.time_s:.1f} s: {STATE_LABELS[bench.state]}, "
            f"lights {bench.lights()}, alarms: {', '.join(alarms) or 'none'}"
        )
    finally:
        if args.csv:
            bench.recorder.write_csv(Path(args.csv))
        bench.close()
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="landing-gear-sim", description=__doc__)
    parser.add_argument("scenario", help="scenario name in scenarios/ or path to a YAML file")
    parser.add_argument("--stop-at-s", type=float, help="end time (default: the scenario's)")
    parser.add_argument("--csv", help="write the recorded trace to this file")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    if not resolve_scenario(args.scenario).exists():
        print(f"no scenario {args.scenario}", file=sys.stderr)
        return 2
    return run(args)
