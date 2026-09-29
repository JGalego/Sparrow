#!/usr/bin/env bash
# Software in the loop: the controller runtime binary (the process that runs
# on the target) against the plant over UDP, with the desktop HMI.
#   scripts/run-sil.sh [--speed X] [--scenario NAME]
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
build="${SPARROW_BUILD_DIR:-$root/build/host}"
python="${PYTHON:-$root/.venv/bin/python}"
[ -x "$python" ] || python=python3
speed=1
plant_args=()
while [ "$#" -gt 0 ]; do
    case "$1" in
        --speed) speed="$2"; shift 2 ;;
        *) plant_args+=("$1"); shift ;;
    esac
done

"$python" -m smart_boiler.plant_server --speed "$speed" "${plant_args[@]}" &
plant=$!
"$build/examples/smart-boiler/boiler_runtime" --speed "$speed" &
runtime=$!
trap 'kill "$plant" "$runtime" 2>/dev/null || true' EXIT

"$build/examples/smart-boiler/boiler_hmi" --sim-tools
