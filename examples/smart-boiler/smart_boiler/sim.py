"""Desktop simulator: plant + C controller, paced in real time, talking to the HMI over UDP."""

from __future__ import annotations

import argparse
import ctypes
import sys
import time
from pathlib import Path

from sparrow.simulation.link import Endpoint, Frame
from sparrow.simulation.plant import UnknownFault
from sparrow.simulation.scenario import ScenarioPlayer, load_scenario

from .bench import STEP_MS, BoilerBench
from .model import BoilerCommands, BoilerFaultInjection, FrameKind, PlantFault
from .plant import BoilerParams

HMI_PORT = 47301
SIM_PORT = 47302
SCENARIO_DIR = Path(__file__).resolve().parents[1] / "scenarios"
COMMAND_FIELDS = {name for name, _ in BoilerCommands._fields_ if not name.startswith("reserved")}


def apply_injection(target, payload: bytes) -> None:
    """Applies a FAULT_INJECTION payload to anything with inject() and clear()."""
    if len(payload) != ctypes.sizeof(BoilerFaultInjection):
        return
    request = BoilerFaultInjection.from_buffer_copy(payload)
    try:
        if request.action == 1:
            target.inject(PlantFault(request.fault).name)
        elif request.action == 0:
            target.clear(PlantFault(request.fault).name)
        elif request.action == 2:
            target.clear()
    except (ValueError, UnknownFault):
        pass


def apply_frame(bench: BoilerBench, frame: Frame) -> None:
    if frame.kind == FrameKind.COMMANDS and len(frame.payload) == ctypes.sizeof(BoilerCommands):
        commands = BoilerCommands.from_buffer_copy(frame.payload)
        bench.command(**{n: getattr(commands, n) for n in COMMAND_FIELDS if getattr(commands, n)})
    elif frame.kind == FrameKind.FAULT_INJECTION:
        apply_injection(bench, frame.payload)


def resolve_scenario(name: str) -> Path:
    path = Path(name)
    return path if path.exists() else SCENARIO_DIR / f"{name}.yaml"


def run(args: argparse.Namespace) -> int:
    scenario = load_scenario(resolve_scenario(args.scenario)) if args.scenario else None
    params = BoilerParams(**(scenario.plant if scenario else {}))
    bench = BoilerBench(params)
    player = ScenarioPlayer(bench, scenario) if scenario else None
    link = Endpoint(listen_port=args.port, peer_port=args.hmi_port)
    period_s = STEP_MS / 1000.0 / args.speed
    next_tick = time.monotonic()
    try:
        while args.stop_at_s is None or bench.time_s < args.stop_at_s:
            for frame in link.receive():
                apply_frame(bench, frame)
            paused = args.pause_at_s is not None and bench.time_s >= args.pause_at_s
            if not paused:
                if player:
                    player.apply_due_events()
                bench.step()
            link.send(FrameKind.STATUS, bench.status)
            next_tick += period_s if not paused else STEP_MS / 1000.0
            delay = next_tick - time.monotonic()
            if delay > 0:
                time.sleep(delay)
            else:
                next_tick = time.monotonic()
    except KeyboardInterrupt:
        pass
    finally:
        if args.csv:
            bench.recorder.write_csv(Path(args.csv))
        link.close()
        bench.close()
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="smart-boiler-sim", description=__doc__)
    parser.add_argument("--scenario", help="scenario name in scenarios/ or path to a YAML file")
    parser.add_argument("--speed", type=float, default=1.0, help="simulated seconds per second")
    parser.add_argument("--port", type=int, default=SIM_PORT, help="UDP port to listen on")
    parser.add_argument("--hmi-port", type=int, default=HMI_PORT, help="UDP port of the HMI")
    parser.add_argument("--pause-at-s", type=float, help="hold the simulation at this time")
    parser.add_argument("--stop-at-s", type=float, help="exit at this simulation time")
    parser.add_argument("--csv", help="write the recorded trace to this file on exit")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    if args.speed <= 0:
        print("--speed must be positive", file=sys.stderr)
        return 2
    return run(args)
