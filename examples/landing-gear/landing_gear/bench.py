"""Closed-loop bench: C controller against the Python plant."""

from __future__ import annotations

from dataclasses import dataclass, field

from sparrow.simulation.plant import UnknownFault
from sparrow.simulation.recorder import Recorder

from .controller import GearController
from .model import GearCommands, GearConfig, GearInputs, GearStatus, State, default_config
from .plant import CONTROLS, LEGS, GearParams, GearPlant

STEP_MS = 50
COMMAND_FIELDS = {name for name, _ in GearCommands._fields_ if not name.startswith("reserved")}
OUTPUT_FIELDS = ("down_valve", "up_valve", "uplock_release", "handle_lock", "gear_horn")


@dataclass
class GearBench:
    """Steps the controller and the plant in lock-step at a fixed period.

    Each step: the plant's sensors are sampled, the controller computes its
    outputs and status, then the plant advances under those outputs.
    command() takes both glareshield commands (mute, reset), which go to the
    controller for one step, and pilot or flight-path controls
    (gear_handle_down, alternate_extend, vertical_speed_fpm,
    acceleration_kt_per_s), which go to the plant.
    """

    params: GearParams = field(default_factory=GearParams)
    config: GearConfig = field(default_factory=default_config)
    recorder: Recorder = field(default_factory=Recorder)

    def __post_init__(self) -> None:
        self.plant = GearPlant(self.params)
        self.controller = GearController(self.config)
        self.time_ms = 0
        self._pending = GearCommands()
        self.outputs = {name: 0 for name in (*OUTPUT_FIELDS, "master_warning")}
        self.status: GearStatus = self.controller.status()

    @property
    def time_s(self) -> float:
        return self.time_ms / 1000.0

    # Harness protocol used by scenarios

    def inject(self, fault: str) -> None:
        self.plant.inject(fault)

    def clear(self, fault: str | None = None) -> None:
        self.plant.clear(fault)

    def command(self, **fields: float) -> None:
        for name, value in fields.items():
            if name in COMMAND_FIELDS:
                setattr(self._pending, name, int(value))
            elif name in CONTROLS:
                self.plant.control(name, value)
            else:
                raise UnknownFault(f"unknown command field '{name}'")

    def step(self) -> None:
        sensors = self.plant.sensors()
        names = [name for name, _ in GearInputs._fields_ if name in sensors]
        inputs = GearInputs(**{name: sensors[name] for name in names})
        outputs = self.controller.step(inputs, self._pending, STEP_MS)
        self._pending = GearCommands()
        self.outputs = {name: getattr(outputs, name) for name in self.outputs}
        self.plant.step(STEP_MS / 1000.0, self.outputs)
        self.time_ms += STEP_MS
        self.status = self.controller.status()
        self.recorder.record(self.sample())

    # Convenience for tests and tools

    def run(self, seconds: float) -> None:
        for _ in range(round(seconds * 1000 / STEP_MS)):
            self.step()

    def run_until(self, predicate, timeout_s: float) -> bool:
        """Steps until predicate(bench) is true. Returns False on timeout."""
        for _ in range(round(timeout_s * 1000 / STEP_MS)):
            if predicate(self):
                return True
            self.step()
        return predicate(self)

    @property
    def state(self) -> State:
        return State(self.status.state)

    def raised(self, fault: int) -> bool:
        mask = self.status.alarms_active | self.status.alarms_latched
        return bool(mask & (1 << fault))

    def lights(self) -> str:
        """Gear lights from the plant's lock sensors: G down and locked, R in transit, - up."""
        marks = []
        for leg in self.plant.legs.values():
            marks.append("G" if leg.downlocked else "-" if leg.uplocked else "R")
        return " ".join(marks)

    def sample(self) -> dict[str, float]:
        plant = self.plant
        row: dict[str, float] = {
            "time_s": self.time_s,
            "state": self.status.state,
            "altitude_ft": round(plant.altitude_ft, 1),
            "airspeed_kt": round(plant.airspeed_kt, 1),
            "hydraulic_psi": round(plant.pressure_psi),
            "on_ground": int(plant.on_ground),
            "gear_handle_down": int(plant.gear_handle_down),
            "alternate_extend": int(plant.alternate_extend),
        }
        row.update({f"{leg}_position": round(plant.legs[leg].position, 3) for leg in LEGS})
        row.update(self.outputs)
        row["alarms_active"] = self.status.alarms_active
        row["alarms_latched"] = self.status.alarms_latched
        return row

    def close(self) -> None:
        self.controller.close()
