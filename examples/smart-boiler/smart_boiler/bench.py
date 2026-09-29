"""Closed-loop bench: C controller against the Python plant."""

from __future__ import annotations

from dataclasses import dataclass, field

from sparrow.simulation.plant import UnknownFault
from sparrow.simulation.recorder import Recorder

from .controller import BoilerController
from .model import (
    BoilerCommands,
    BoilerConfig,
    BoilerInputs,
    BoilerStatus,
    Fault,
    State,
    default_config,
)
from .plant import BoilerParams, BoilerPlant, SensorRanges

STEP_MS = 100


def sensor_ranges(config: BoilerConfig) -> SensorRanges:
    """Transmitter ranges matching the controller's scaling configuration."""
    return SensorRanges(
        temperature_c=(config.temperature_min_c, config.temperature_max_c),
        pressure_bar=(0.0, config.pressure_max_bar),
        flow_lpm=(0.0, config.flow_max_lpm),
        valve_position_pct=(0.0, 100.0),
    )


@dataclass
class BoilerBench:
    """Steps the controller and the plant in lock-step at a fixed period.

    Each step: the plant's sensors are sampled, the controller computes its
    outputs and status, then the plant advances under those outputs.
    """

    params: BoilerParams = field(default_factory=BoilerParams)
    config: BoilerConfig = field(default_factory=default_config)
    recorder: Recorder = field(default_factory=Recorder)

    def __post_init__(self) -> None:
        self.plant = BoilerPlant(self.params, sensor_ranges(self.config))
        self.controller = BoilerController(self.config)
        self.time_ms = 0
        self._pending = BoilerCommands()
        self.status: BoilerStatus = self.controller.status()

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
            if name not in {field_name for field_name, _ in BoilerCommands._fields_}:
                raise UnknownFault(f"unknown command field '{name}'")
            setattr(self._pending, name, value)

    def step(self) -> None:
        sensors = self.plant.sensors()
        inputs = BoilerInputs(
            temperature_ma=sensors["temperature_ma"],
            pressure_ma=sensors["pressure_ma"],
            flow_ma=sensors["flow_ma"],
            valve_position_ma=sensors["valve_position_ma"],
            pump_running=int(sensors["pump_running"]),
        )
        outputs = self.controller.step(inputs, self._pending, STEP_MS)
        self._pending = BoilerCommands()
        self.plant.step(
            STEP_MS / 1000.0,
            {
                "heater_power_pct": outputs.heater_power_pct,
                "heater_contactor": outputs.heater_contactor,
                "pump_run": outputs.pump_run,
                "valve_open": outputs.valve_open,
            },
        )
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

    def raised(self, fault: Fault) -> bool:
        mask = self.status.alarms_active | self.status.alarms_latched
        return bool(mask & (1 << fault))

    def sample(self) -> dict[str, float]:
        status = self.status
        return {
            "time_s": self.time_s,
            "state": status.state,
            "temperature_c": self.plant.temperature_c,
            "measured_temperature_c": status.temperature_c,
            "setpoint_c": status.setpoint_c,
            "pressure_bar": self.plant.pressure_bar,
            "flow_lpm": self.plant.flow_lpm,
            "valve_position_pct": self.plant.valve_position_pct,
            "heater_power_pct": status.heater_power_pct,
            "heater_contactor": status.heater_contactor,
            "pump_on": status.pump_on,
            "alarms_active": status.alarms_active,
            "alarms_latched": status.alarms_latched,
        }

    def close(self) -> None:
        self.controller.close()
