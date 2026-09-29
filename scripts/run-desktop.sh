#!/usr/bin/env bash
# Runs the Smart Boiler on the desktop: plant simulator, C controller (loaded by
# the simulator) and the SDL HMI. Extra arguments go to the simulator, e.g.
#   scripts/run-desktop.sh --scenario over-temperature --speed 10
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
build="${SPARROW_BUILD_DIR:-$root/build/host}"
python="${PYTHON:-$root/.venv/bin/python}"
[ -x "$python" ] || python=python3

simulator_args=("$@")
[ "$#" -gt 0 ] || simulator_args=(--scenario demo --speed 5)

"$python" -m smart_boiler "${simulator_args[@]}" &
simulator=$!
trap 'kill "$simulator" 2>/dev/null || true' EXIT

"$build/examples/smart-boiler/boiler_hmi" --sim-tools
