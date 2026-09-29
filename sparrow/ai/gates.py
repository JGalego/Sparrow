"""Runs the project's deterministic gates (`ai.gates` in sparrow.yaml)."""

from __future__ import annotations

import subprocess
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class GateResult:
    command: str
    returncode: int
    output: str

    @property
    def passed(self) -> bool:
        return self.returncode == 0


def run_gates(
    commands: tuple[str, ...], repo: Path, stop_on_failure: bool = True
) -> list[GateResult]:
    results = []
    for command in commands:
        completed = subprocess.run(
            command, shell=True, cwd=repo, capture_output=True, text=True, check=False
        )
        results.append(
            GateResult(command, completed.returncode, completed.stdout + completed.stderr)
        )
        if stop_on_failure and completed.returncode != 0:
            break
    return results
