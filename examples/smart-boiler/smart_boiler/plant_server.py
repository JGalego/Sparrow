"""The plant alone, served over UDP to a controller runtime (software in the loop).

The plant receives OUTPUTS frames, advances in real time (scaled by
--speed) and sends INPUTS frames. It holds the last outputs it received, so
a controller that stops talking leaves its actuators where they were, as a
real plant would. Fault injection frames forwarded by the runtime are applied.

    python -m smart_boiler.plant_server --speed 1
    build/host/examples/smart-boiler/boiler_runtime
"""

from __future__ import annotations

import argparse
import ctypes
import sys
import time

from sparrow.simulation.link import Endpoint
from sparrow.simulation.scenario import load_scenario

from .bench import STEP_MS, sensor_ranges
from .model import BoilerInputs, BoilerOutputs, FrameKind, default_config
from .plant import BoilerParams, BoilerPlant
from .sim import apply_injection, resolve_scenario

RUNTIME_PORT = 47311
PLANT_PORT = 47312
SAFE_ACTUATORS = {"heater_power_pct": 0.0, "heater_contactor": 0, "pump_run": 0, "valve_open": 0}


def to_inputs(sensors) -> BoilerInputs:
    return BoilerInputs(
        temperature_ma=sensors["temperature_ma"],
        pressure_ma=sensors["pressure_ma"],
        flow_ma=sensors["flow_ma"],
        valve_position_ma=sensors["valve_position_ma"],
        pump_running=int(sensors["pump_running"]),
    )


def to_actuators(outputs: BoilerOutputs) -> dict[str, float]:
    return {
        "heater_power_pct": outputs.heater_power_pct,
        "heater_contactor": outputs.heater_contactor,
        "pump_run": outputs.pump_run,
        "valve_open": outputs.valve_open,
    }


class PlantServer:
    def __init__(self, plant: BoilerPlant, endpoint: Endpoint):
        self.plant = plant
        self.endpoint = endpoint
        self.actuators = dict(SAFE_ACTUATORS)
        self.outputs_received = 0

    def poll(self) -> None:
        for frame in self.endpoint.receive():
            if frame.kind == FrameKind.OUTPUTS and len(frame.payload) == ctypes.sizeof(
                BoilerOutputs
            ):
                self.actuators = to_actuators(BoilerOutputs.from_buffer_copy(frame.payload))
                self.outputs_received += 1
            elif frame.kind == FrameKind.FAULT_INJECTION:
                apply_injection(self.plant, frame.payload)

    def tick(self, dt_s: float) -> None:
        self.poll()
        self.plant.step(dt_s, self.actuators)
        self.endpoint.send(FrameKind.INPUTS, to_inputs(self.plant.sensors()))


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="smart-boiler-plant", description=__doc__)
    parser.add_argument("--scenario", help="takes the plant parameters from a scenario")
    parser.add_argument("--speed", type=float, default=1.0, help="simulated seconds per second")
    parser.add_argument("--host", default="127.0.0.1", help="address of the controller runtime")
    parser.add_argument("--bind", default="127.0.0.1", help="address to listen on")
    parser.add_argument("--stop-at-s", type=float, help="exit at this simulation time")
    args = parser.parse_args(argv)
    if args.speed <= 0:
        print("--speed must be positive", file=sys.stderr)
        return 2
    plant_params = load_scenario(resolve_scenario(args.scenario)).plant if args.scenario else {}
    plant = BoilerPlant(BoilerParams(**plant_params), sensor_ranges(default_config()))
    endpoint = Endpoint(PLANT_PORT, RUNTIME_PORT, host=args.bind, peer_host=args.host)
    server = PlantServer(plant, endpoint)
    step_s = STEP_MS / 1000.0
    next_tick = time.monotonic()
    elapsed_s = 0.0
    try:
        while args.stop_at_s is None or elapsed_s < args.stop_at_s:
            server.tick(step_s)
            elapsed_s += step_s
            next_tick += step_s / args.speed
            time.sleep(max(0.0, next_tick - time.monotonic()))
    except KeyboardInterrupt:
        pass
    finally:
        endpoint.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
