#!/usr/bin/env bash
# snapshot.sh <scenario> <sim-seconds> <output.png> [hmi args...]
# Runs a scenario at 30x until the given simulation time, holds it there, and
# saves the HMI screen as a PNG. Needs the Pillow package for the conversion.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
build="${SPARROW_BUILD_DIR:-$root/build/host}"
python="${PYTHON:-$root/.venv/bin/python}"
scenario="$1"; seconds="$2"; output="$3"; shift 3
ppm="$(mktemp --suffix=.ppm)"
trap 'rm -f "$ppm"; kill "${simulator:-0}" 2>/dev/null || true' EXIT

"$python" -m smart_boiler --scenario "$scenario" --speed "${SNAPSHOT_SPEED:-30}" --pause-at-s "$seconds" &
simulator=$!
"$build/examples/smart-boiler/boiler_hmi" --headless --snapshot "$ppm" \
    --snapshot-at-ms "$((seconds * 1000))" --timeout-s 180 "$@"
"$python" -c "import sys; from PIL import Image; Image.open(sys.argv[1]).save(sys.argv[2])" "$ppm" "$output"
