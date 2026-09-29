"""Timed scenarios: fault injection and operator commands at given simulation times."""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Protocol

import yaml


class ScenarioError(Exception):
    pass


ACTIONS = ("inject", "clear", "command")


@dataclass(frozen=True)
class Event:
    at_s: float
    action: str
    argument: Any


@dataclass
class Scenario:
    name: str
    description: str
    plant: dict[str, float] = field(default_factory=dict)
    events: list[Event] = field(default_factory=list)


class Harness(Protocol):
    """What a scenario needs from a simulation bench."""

    time_s: float

    def inject(self, fault: str) -> None: ...

    def clear(self, fault: str | None = None) -> None: ...

    def command(self, **fields: float) -> None: ...

    def step(self) -> None: ...


def load_scenario(path: Path) -> Scenario:
    raw = yaml.safe_load(path.read_text(encoding="utf-8"))
    events = []
    for item in raw.get("events", []):
        action = item.get("action")
        if action not in ACTIONS:
            raise ScenarioError(f"{path.name}: unknown action '{action}'")
        argument = item.get("fault") if action != "command" else item.get("fields", {})
        events.append(Event(float(item["at_s"]), action, argument))
    events.sort(key=lambda e: e.at_s)
    return Scenario(raw["name"], raw.get("description", "").strip(), raw.get("plant", {}), events)


class ScenarioPlayer:
    """Applies scenario events to a harness as its time advances."""

    def __init__(self, harness: Harness, scenario: Scenario):
        self._harness = harness
        self._pending = list(scenario.events)

    def apply_due_events(self) -> None:
        while self._pending and self._pending[0].at_s <= self._harness.time_s:
            event = self._pending.pop(0)
            if event.action == "inject":
                self._harness.inject(event.argument)
            elif event.action == "clear":
                self._harness.clear(event.argument)
            else:
                self._harness.command(**event.argument)
