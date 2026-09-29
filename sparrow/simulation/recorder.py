"""Time series recording for tests and offline analysis."""

from __future__ import annotations

import csv
from pathlib import Path


class Recorder:
    def __init__(self) -> None:
        self.rows: list[dict[str, float]] = []

    def record(self, row: dict[str, float]) -> None:
        self.rows.append(dict(row))

    def column(self, name: str) -> list[float]:
        return [row[name] for row in self.rows]

    def write_csv(self, path: Path) -> None:
        if not self.rows:
            return
        with path.open("w", newline="", encoding="utf-8") as handle:
            writer = csv.DictWriter(handle, fieldnames=list(self.rows[0]))
            writer.writeheader()
            writer.writerows(self.rows)
