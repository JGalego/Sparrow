"""Model of a tricycle landing gear, its hydraulic system and the aircraft around it.

The model is deliberately small. It exists to exercise the controller with
plausible timing and failure modes, not to predict a real aircraft:

* one hydraulic system whose pressure follows the engine-driven pump with a
  first-order lag, and decays when the pump fails;
* three legs, each with a position between 0 (up) and 1 (down), a mechanical
  uplock and a spring downlock. A leg moves under hydraulic power only when
  the pressure is at least actuation_min_psi. Its uplock opens under the
  gear-down pressure or the alternate-extension release, after which a leg
  without hydraulic power falls to down and locked under gravity and air load;
* the aircraft as airspeed and height above flat terrain, with a take-off
  rotation and a touchdown, a radio altimeter that reads up to 2500 ft, and
  squat switches that are made only for main legs down on the ground;
* the pilot's gear lever, which the lever lock holds down while energized,
  and the alternate extension handle.

All parameters live in GearParams. The plant knows nothing about the
controller: it consumes actuator commands and produces sensor signals.
"""

from __future__ import annotations

from collections.abc import Mapping
from dataclasses import dataclass, field

from sparrow.simulation.plant import Plant, UnknownFault

from .model import PlantFault

MAX_SUBSTEP_S = 0.05
LEGS = ("nose", "left", "right")
CONTROLS = ("gear_handle_down", "alternate_extend", "vertical_speed_fpm", "acceleration_kt_per_s")


@dataclass(frozen=True)
class GearParams:
    initial_altitude_ft: float = 0.0
    initial_airspeed_kt: float = 0.0
    vertical_speed_fpm: float = 0.0
    acceleration_kt_per_s: float = 0.0
    rotate_kt: float = 0.0  # 0: no take-off; otherwise lift off at this speed
    climb_fpm: float = 2000.0
    gear_down: bool = True
    nominal_psi: float = 3000.0
    pressure_time_constant_s: float = 1.0
    actuation_min_psi: float = 1000.0
    extend_time_s: float = 7.0
    retract_time_s: float = 8.0
    freefall_time_s: float = 12.0
    radio_altimeter_range_ft: float = 2500.0
    hydraulic_span_psi: float = 5000.0  # transmitter reading at 20 mA


@dataclass
class Leg:
    position: float  # 0 up, 1 down
    uplocked: bool
    downlocked: bool


@dataclass
class GearPlant(Plant):
    params: GearParams = field(default_factory=GearParams)

    def __post_init__(self) -> None:
        p = self.params
        down = p.gear_down
        self.legs = {name: Leg(1.0 if down else 0.0, not down, down) for name in LEGS}
        self.pressure_psi = p.nominal_psi
        self.altitude_ft = p.initial_altitude_ft
        self.airspeed_kt = p.initial_airspeed_kt
        self.vertical_speed_fpm = p.vertical_speed_fpm
        self.acceleration_kt_per_s = p.acceleration_kt_per_s
        self.on_ground = p.initial_altitude_ft <= 0.0
        self.gear_handle_down = down
        self.alternate_extend = False
        self.handle_locked = False
        self.lever_blocked = False  # the last lever-up attempt met the lever lock
        self.touchdowns: list[bool] = []  # one entry per touchdown: all legs down and locked
        self._faults: set[str] = set()
        self._rotated = False  # take-off happens at most once

    # Pilot and flight path

    def control(self, name: str, value: float) -> None:
        if name not in CONTROLS:
            raise UnknownFault(f"unknown control '{name}'")
        if name == "gear_handle_down":
            wants_down = bool(value)
            self.lever_blocked = not wants_down and self.handle_locked and self.gear_handle_down
            if not self.lever_blocked:
                self.gear_handle_down = wants_down
        elif name == "alternate_extend":
            self.alternate_extend = bool(value)
        else:
            setattr(self, name, float(value))

    # Plant interface

    def inject(self, fault: str) -> None:
        if fault not in PlantFault.__members__ or fault == "NONE":
            raise UnknownFault(fault)
        self._faults.add(fault)

    def clear(self, fault: str | None = None) -> None:
        if fault is None:
            self._faults.clear()
        elif fault in PlantFault.__members__:
            self._faults.discard(fault)
        else:
            raise UnknownFault(fault)

    def active_faults(self) -> frozenset[str]:
        return frozenset(self._faults)

    def step(self, dt_s: float, actuators: Mapping[str, float]) -> None:
        self.handle_locked = bool(actuators.get("handle_lock", 0))
        remaining = dt_s
        while remaining > 1e-9:
            dt = min(remaining, MAX_SUBSTEP_S)
            self._hydraulics(dt)
            for name, leg in self.legs.items():
                self._move_leg(name, leg, dt, actuators)
            self._fly(dt)
            remaining -= dt

    def sensors(self) -> Mapping[str, float]:
        p = self.params
        ra_valid = (
            self.altitude_ft <= p.radio_altimeter_range_ft
            and "RADIO_ALTIMETER_FAILED" not in self._faults
        )
        wow_stuck = "WOW_STUCK_AIR" in self._faults
        values: dict[str, float] = {
            "hydraulic_pressure_ma": 4.0 + 16.0 * self.pressure_psi / p.hydraulic_span_psi,
            "airspeed_kt": self.airspeed_kt,
            "airspeed_valid": 1,
            "radio_altitude_ft": self.altitude_ft if ra_valid else 0.0,
            "radio_altitude_valid": int(ra_valid),
            "gear_handle_down": int(self.gear_handle_down),
            "alternate_extend": int(self.alternate_extend),
        }
        for name, leg in self.legs.items():
            values[f"{name}_downlock"] = int(leg.downlocked)
            values[f"{name}_uplock"] = int(leg.uplocked)
        for name in ("left", "right"):
            compressed = self.on_ground and self.legs[name].downlocked
            values[f"{name}_wow"] = int(compressed and not wow_stuck)
        return values

    # Dynamics

    def _hydraulics(self, dt: float) -> None:
        p = self.params
        target = 0.0 if "HYD_PUMP_FAILED" in self._faults else p.nominal_psi
        self.pressure_psi += (target - self.pressure_psi) * dt / p.pressure_time_constant_s

    def _move_leg(self, name: str, leg: Leg, dt: float, actuators: Mapping[str, float]) -> None:
        p = self.params
        if name == "nose" and "NOSE_GEAR_JAMMED" in self._faults:
            return
        powered = self.pressure_psi >= p.actuation_min_psi
        up = bool(actuators.get("up_valve", 0)) and powered
        down = bool(actuators.get("down_valve", 0)) and powered
        release = bool(actuators.get("uplock_release", 0))
        if up and not self.on_ground:
            leg.downlocked = False
            if not leg.uplocked:
                leg.position = max(0.0, leg.position - dt / p.retract_time_s)
                leg.uplocked = leg.position == 0.0
            return
        if leg.uplocked and (down or release):
            leg.uplocked = False
        if leg.uplocked or leg.downlocked:
            return
        travel = p.extend_time_s if down else p.freefall_time_s
        leg.position = min(1.0, leg.position + dt / travel)
        leg.downlocked = leg.position == 1.0

    def _fly(self, dt: float) -> None:
        p = self.params
        self.airspeed_kt = max(0.0, self.airspeed_kt + self.acceleration_kt_per_s * dt)
        if self.on_ground:
            rotate = p.rotate_kt > 0.0 and self.airspeed_kt >= p.rotate_kt
            if rotate and not self._rotated:
                self._rotated = True
                self.vertical_speed_fpm = p.climb_fpm
            if self.vertical_speed_fpm <= 0.0:
                return
            self.on_ground = False
        self.altitude_ft += self.vertical_speed_fpm / 60.0 * dt
        if self.altitude_ft <= 0.0:
            self.altitude_ft = 0.0
            self.vertical_speed_fpm = 0.0
            self.on_ground = True
            self.touchdowns.append(all(leg.downlocked for leg in self.legs.values()))
