"""Plant model interface."""

from __future__ import annotations

from abc import ABC, abstractmethod
from collections.abc import Mapping


class UnknownFault(KeyError):
    pass


class Plant(ABC):
    """A physical process advanced in fixed time steps.

    Implementations must be deterministic: the same sequence of step() and
    inject() calls after construction gives the same results. Randomness, if
    any, comes from a seeded generator owned by the plant.
    """

    @abstractmethod
    def step(self, dt_s: float, actuators: Mapping[str, float]) -> None:
        """Advances the process by dt_s seconds under the given actuator values."""

    @abstractmethod
    def sensors(self) -> Mapping[str, float]:
        """Current sensor signals as the field wiring would present them."""

    @abstractmethod
    def inject(self, fault: str) -> None:
        """Activates a named fault. Raises UnknownFault for unknown names."""

    @abstractmethod
    def clear(self, fault: str | None = None) -> None:
        """Removes one fault, or all faults if fault is None."""

    @abstractmethod
    def active_faults(self) -> frozenset[str]: ...
