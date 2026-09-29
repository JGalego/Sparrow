#!/usr/bin/env bash
# record-gif.sh <scenario> <speed> <frames> <every-ms> <output.gif> [hmi args...]
# Runs a scenario in the simulator, records HMI frames at a fixed wall-clock
# interval and assembles them into a looping GIF. Needs Pillow.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
build="${SPARROW_BUILD_DIR:-$root/build/host}"
python="${PYTHON:-$root/.venv/bin/python}"
scenario="$1"; speed="$2"; frames="$3"; every_ms="$4"; output="$5"; shift 5
frames_dir="$(mktemp -d)"
trap 'rm -rf "$frames_dir"; kill "${simulator:-0}" 2>/dev/null || true' EXIT

"$python" -m smart_boiler --scenario "$scenario" --speed "$speed" &
simulator=$!
"$build/examples/smart-boiler/boiler_hmi" --headless --record "$frames_dir" \
    --record-frames "$frames" --record-every-ms "$every_ms" --timeout-s 300 "$@"
"$python" "$root/scripts/frames-to-gif.py" "$frames_dir" "$output" --frame-ms "$every_ms"
