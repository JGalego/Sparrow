"""Lumped-parameter model of a pressurized hot-water boiler and its field devices.

The model is deliberately small. It exists to exercise the controller with
plausible dynamics and failure modes, not to predict a real installation:

* one thermal mass heated by an electric heater, losing heat to the ambient and
  to a heating circuit whose heat transfer scales with flow;
* a pump with first-order spin-up and a motorized valve with limited speed;
* pressure from a fill pressure, thermal expansion and pump head against a
  closed valve;
* 4-20 mA transmitters that can fail open, short, or read high.

All parameters live in BoilerParams. The plant knows nothing about the
controller: it consumes actuator commands and produces loop currents.
"""

from __future__ import annotations

import random
from collections.abc import Mapping
from dataclasses import dataclass

from sparrow.simulation.plant import Plant, UnknownFault

from .model import PlantFault

OPEN_LOOP_MA = 0.0
SHORT_LOOP_MA = 22.0
MAX_SUBSTEP_S = 0.05


@dataclass(frozen=True)
class BoilerParams:
    thermal_capacity_j_per_k: float = 125_000.0
    heater_power_w: float = 15_000.0
    ambient_c: float = 20.0
    loss_w_per_k: float = 6.0
    load_w_per_k: float = 100.0
    pump_nominal_lpm: float = 40.0
    pump_spin_up_s: float = 1.0
    pump_head_bar: float = 3.0
    valve_travel_pct_per_s: float = 12.5
    fill_pressure_bar: float = 1.2
    thermal_pressure_bar_per_k: float = 0.02
    expansion_failure_factor: float = 4.0
    leak_bar_per_s: float = 0.03
    temperature_offset_fault_c: float = 8.0
    initial_temperature_c: float = 20.0
    noise: bool = True
    seed: int = 1


@dataclass(frozen=True)
class SensorRanges:
    """Engineering range mapped to 4-20 mA for each transmitter."""

    temperature_c: tuple[float, float] = (0.0, 150.0)
    pressure_bar: tuple[float, float] = (0.0, 6.0)
    flow_lpm: tuple[float, float] = (0.0, 100.0)
    valve_position_pct: tuple[float, float] = (0.0, 100.0)


NOISE_TEMPERATURE_C = 0.03
NOISE_PRESSURE_BAR = 0.004
NOISE_FLOW_LPM = 0.15
# Flow transmitters report zero below their low-flow cut-off.
FLOW_CUTOFF_LPM = 0.5


class BoilerPlant(Plant):
    def __init__(self, params: BoilerParams | None = None, ranges: SensorRanges | None = None):
        self.params = params or BoilerParams()
        self.ranges = ranges or SensorRanges()
        self._random = random.Random(self.params.seed)
        self._faults: set[PlantFault] = set()
        self.temperature_c = self.params.initial_temperature_c
        self.valve_position_pct = 0.0
        self.pump_speed = 0.0
        self.fill_pressure_bar = self.params.fill_pressure_bar
        self._pump_on = False
        self._heater_pct = 0.0

    # Plant interface

    def inject(self, fault: str) -> None:
        self._faults.add(self._parse(fault))

    def clear(self, fault: str | None = None) -> None:
        if fault is None:
            self._faults.clear()
        else:
            self._faults.discard(self._parse(fault))

    def active_faults(self) -> frozenset[str]:
        return frozenset(f.name for f in self._faults)

    def step(self, dt_s: float, actuators: Mapping[str, float]) -> None:
        heater = self._applied_heater_fraction(actuators)
        self._pump_on = bool(actuators["pump_run"])
        valve_target = 100.0 if actuators["valve_open"] else 0.0
        remaining = dt_s
        while remaining > 1e-12:
            slice_s = min(remaining, MAX_SUBSTEP_S)
            self._advance_valve(slice_s, valve_target)
            self._advance_pump(slice_s)
            self._advance_leak(slice_s)
            self._advance_temperature(slice_s, heater)
            remaining -= slice_s

    def sensors(self) -> Mapping[str, float]:
        ranges = self.ranges
        return {
            "temperature_ma": self._loop_current(
                self._temperature_reading(),
                ranges.temperature_c,
                NOISE_TEMPERATURE_C,
                open_fault=PlantFault.TEMP_SENSOR_OPEN,
                short_fault=PlantFault.TEMP_SENSOR_SHORT,
            ),
            "pressure_ma": self._loop_current(
                self.pressure_bar,
                ranges.pressure_bar,
                NOISE_PRESSURE_BAR,
                open_fault=PlantFault.PRESSURE_SENSOR_OPEN,
            ),
            "flow_ma": self._loop_current(
                self.flow_lpm,
                ranges.flow_lpm,
                NOISE_FLOW_LPM,
                open_fault=PlantFault.FLOW_SENSOR_OPEN,
                cutoff=FLOW_CUTOFF_LPM,
            ),
            "valve_position_ma": self._loop_current(
                self.valve_position_pct,
                ranges.valve_position_pct,
                0.0,
                open_fault=PlantFault.VALVE_SENSOR_OPEN,
            ),
            "pump_running": float(self.pump_running_feedback),
        }

    # Process quantities

    @property
    def pump_running_feedback(self) -> bool:
        return self._pump_on and PlantFault.PUMP_TRIPPED not in self._faults

    @property
    def flow_lpm(self) -> float:
        if PlantFault.PUMP_SEIZED in self._faults:
            return 0.0
        return self.pump_speed * self.params.pump_nominal_lpm * self.valve_position_pct / 100.0

    @property
    def pressure_bar(self) -> float:
        p = self.params
        expansion = p.thermal_pressure_bar_per_k * (self.temperature_c - p.ambient_c)
        expansion *= self.fill_pressure_bar / p.fill_pressure_bar
        if PlantFault.EXPANSION_VESSEL_FAILED in self._faults:
            expansion *= p.expansion_failure_factor
        head = p.pump_head_bar * self.pump_speed * (1.0 - self.valve_position_pct / 100.0)
        return max(0.0, self.fill_pressure_bar + expansion + head)

    # Dynamics

    def _applied_heater_fraction(self, actuators: Mapping[str, float]) -> float:
        if not actuators["heater_contactor"]:
            return 0.0
        if PlantFault.HEATER_SSR_SHORTED in self._faults:
            return 1.0
        return max(0.0, min(100.0, actuators["heater_power_pct"])) / 100.0

    def _advance_valve(self, dt_s: float, target: float) -> None:
        if PlantFault.VALVE_STUCK in self._faults:
            return
        step = self.params.valve_travel_pct_per_s * dt_s
        delta = max(-step, min(step, target - self.valve_position_pct))
        self.valve_position_pct += delta

    def _advance_pump(self, dt_s: float) -> None:
        spinning = self._pump_on and PlantFault.PUMP_TRIPPED not in self._faults
        target = 1.0 if spinning else 0.0
        self.pump_speed += (target - self.pump_speed) * min(1.0, dt_s / self.params.pump_spin_up_s)

    def _advance_leak(self, dt_s: float) -> None:
        if PlantFault.LEAK in self._faults:
            self.fill_pressure_bar = max(
                0.0, self.fill_pressure_bar - self.params.leak_bar_per_s * dt_s
            )

    def _advance_temperature(self, dt_s: float, heater_fraction: float) -> None:
        p = self.params
        excess = self.temperature_c - p.ambient_c
        flow_ratio = self.flow_lpm / p.pump_nominal_lpm
        heat_w = p.heater_power_w * heater_fraction
        loss_w = p.loss_w_per_k * excess + p.load_w_per_k * flow_ratio * excess
        self.temperature_c += (heat_w - loss_w) * dt_s / p.thermal_capacity_j_per_k

    # Sensors

    def _temperature_reading(self) -> float:
        offset = (
            self.params.temperature_offset_fault_c
            if PlantFault.TEMP_SENSOR_OFFSET in self._faults
            else 0.0
        )
        return self.temperature_c + offset

    def _noise(self, amplitude: float) -> float:
        if not self.params.noise:
            return 0.0
        return self._random.uniform(-amplitude, amplitude)

    def _loop_current(
        self,
        value: float,
        span: tuple[float, float],
        noise_amplitude: float,
        open_fault: PlantFault,
        short_fault: PlantFault | None = None,
        cutoff: float | None = None,
    ) -> float:
        if open_fault in self._faults:
            return OPEN_LOOP_MA
        if short_fault is not None and short_fault in self._faults:
            return SHORT_LOOP_MA
        low, high = span
        value += self._noise(noise_amplitude)
        if cutoff is not None and value < cutoff:
            value = 0.0
        return 4.0 + 16.0 * (value - low) / (high - low)

    @staticmethod
    def _parse(fault: str) -> PlantFault:
        try:
            return PlantFault[fault]
        except KeyError:
            raise UnknownFault(fault) from None
