#!/usr/bin/env bash
# Cross-compiles for aarch64: the controller and C tests (run under qemu), and
# the full target build with the framebuffer HMI and the controller runtime.
set -euo pipefail
cmake --preset target-aarch64-test
cmake --build --preset target-aarch64-test --parallel
ctest --preset target-aarch64-test
cmake --preset target-aarch64
cmake --build --preset target-aarch64 --parallel
file build/target-aarch64/examples/smart-boiler/boiler_runtime \
     build/target-aarch64/examples/smart-boiler/boiler_hmi
