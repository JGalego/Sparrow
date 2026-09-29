#!/usr/bin/env python3
"""Regenerates the screenshots and the demo GIF in docs/assets from simulator scenarios.

Each shot runs a scenario in the simulator until a fixed simulation time and
captures the HMI headlessly, so the images are reproducible.
"""

import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "docs" / "assets"

# name, scenario, simulation seconds, extra HMI arguments
SHOTS = [
    ("hmi-normal", "normal-operation", 420, []),
    ("hmi-warning", "over-temperature", 240, ["--range", "10m"]),
    ("hmi-fault", "over-temperature", 330, ["--range", "10m"]),
    ("hmi-pump-fault", "pump-seized", 70, ["--range", "2m"]),
    ("hmi-pressure-fault", "pressure-fault", 190, ["--range", "10m"]),
    ("hmi-sensor-fault", "sensor-failure", 70, ["--range", "2m"]),
    ("hmi-sim-panel", "normal-operation", 200, ["--sim-tools", "--show-sim-panel"]),
]


def main() -> int:
    ASSETS.mkdir(parents=True, exist_ok=True)
    for name, scenario, seconds, extra in SHOTS:
        target = ASSETS / f"{name}.png"
        print(f"{name}: {scenario} @ {seconds}s")
        result = subprocess.run(
            [str(ROOT / "scripts" / "snapshot.sh"), scenario, str(seconds), str(target), *extra],
            check=False,
        )
        if result.returncode != 0:
            print(f"failed: {name}", file=sys.stderr)
            return result.returncode
    print("hmi-demo.gif: over-temperature, 30x")
    return subprocess.run(
        [
            str(ROOT / "scripts" / "record-gif.sh"),
            "over-temperature",
            "30",
            "110",
            "100",
            str(ASSETS / "hmi-demo.gif"),
            "--range",
            "2m",
        ],
        check=False,
    ).returncode


if __name__ == "__main__":
    sys.exit(main())
