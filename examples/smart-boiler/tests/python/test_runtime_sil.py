"""The controller runtime binary against the plant over UDP (software in the loop).

These tests run in real time at 10x speed: the runtime steps every 10 ms and
advances the controller by 100 ms, and the plant does the same.
"""

from __future__ import annotations

import ctypes
import os
import signal
import socket
import subprocess
import threading
import time
from pathlib import Path

import pytest
from smart_boiler.bench import STEP_MS, sensor_ranges
from smart_boiler.model import (
    BoilerCommands,
    BoilerFaultInjection,
    BoilerOutputs,
    BoilerStatus,
    Fault,
    FrameKind,
    PlantFault,
    State,
    default_config,
)
from smart_boiler.plant import BoilerParams, BoilerPlant
from smart_boiler.plant_server import PlantServer

from sparrow.simulation.binding import REPO_ROOT
from sparrow.simulation.link import Endpoint

SPEED = 10


def free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


def runtime_binary() -> Path:
    build = Path(os.environ.get("SPARROW_BUILD_DIR", REPO_ROOT / "build" / "host"))
    path = build / "examples" / "smart-boiler" / "boiler_runtime"
    if not path.exists():
        pytest.skip(f"{path} not built")
    return path


class PlantThread(threading.Thread):
    def __init__(self, server: PlantServer):
        super().__init__(daemon=True)
        self.server = server
        self.running = True

    def run(self) -> None:
        step_s = STEP_MS / 1000.0
        while self.running:
            self.server.tick(step_s)
            time.sleep(step_s / SPEED)


class Rig:
    """Runtime process, in-process plant and an HMI-side endpoint on free ports."""

    def __init__(self, tmp_path: Path, watchdog: bool = False, start_plant: bool = True):
        ports = {name: free_port() for name in ("hmi", "hmi_listen", "plant", "plant_listen")}
        self.watchdog = tmp_path / "watchdog" if watchdog else None
        if self.watchdog:
            self.watchdog.write_bytes(b"")
        command = [
            str(runtime_binary()),
            "--speed",
            str(SPEED),
            "--hmi-port",
            str(ports["hmi"]),
            "--hmi-listen-port",
            str(ports["hmi_listen"]),
            "--plant-port",
            str(ports["plant"]),
            "--plant-listen-port",
            str(ports["plant_listen"]),
        ]
        if self.watchdog:
            command += ["--watchdog", str(self.watchdog)]
        self.plant = BoilerPlant(
            BoilerParams(noise=False, initial_temperature_c=78.0), sensor_ranges(default_config())
        )
        self.plant_endpoint = Endpoint(ports["plant"], ports["plant_listen"])
        self.plant_thread = PlantThread(PlantServer(self.plant, self.plant_endpoint))
        self.hmi = Endpoint(ports["hmi"], ports["hmi_listen"])
        self.process = subprocess.Popen(command, stderr=subprocess.PIPE, text=True)
        if start_plant:
            self.plant_thread.start()
        self.status = BoilerStatus()

    def poll(self) -> BoilerStatus:
        for frame in self.hmi.receive():
            if frame.kind == FrameKind.STATUS and len(frame.payload) == ctypes.sizeof(BoilerStatus):
                self.status = BoilerStatus.from_buffer_copy(frame.payload)
        return self.status

    def wait_for(self, predicate, timeout_s: float) -> bool:
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            if predicate(self.poll()):
                return True
            time.sleep(0.005)
        return False

    def command(self, **fields) -> None:
        self.hmi.send(FrameKind.COMMANDS, BoilerCommands(**fields))

    def start_boiler(self) -> None:
        """Waits for STANDBY (start is ignored during INIT), starts, waits for RUNNING."""
        assert self.wait_for(lambda s: s.state == State.STANDBY, 3)
        self.command(start=1)
        assert self.wait_for(lambda s: s.state == State.RUNNING, 5)

    def stop_plant(self) -> None:
        self.plant_thread.running = False
        if self.plant_thread.is_alive():
            self.plant_thread.join()

    def terminate(self) -> str:
        self.process.send_signal(signal.SIGTERM)
        _, stderr = self.process.communicate(timeout=5)
        return stderr

    def close(self) -> None:
        if self.process.poll() is None:
            self.process.kill()
            self.process.wait()
        self.stop_plant()
        self.plant_endpoint.close()
        self.hmi.close()


@pytest.fixture
def rig(tmp_path):
    rig = Rig(tmp_path)
    yield rig
    rig.close()


def raised(status: BoilerStatus, fault: Fault) -> bool:
    return bool((status.alarms_active | status.alarms_latched) & (1 << fault))


@pytest.mark.verifies("REQ-003,REQ-025")
def test_runtime_starts_the_boiler_against_the_plant(rig):
    assert rig.wait_for(lambda s: s.state == State.STANDBY, 3)

    rig.command(start=1)

    assert rig.wait_for(lambda s: s.state == State.RUNNING, 5)
    assert rig.plant.flow_lpm > 20.0


@pytest.mark.verifies("REQ-025")
def test_controller_time_follows_the_scaled_wall_clock(rig):
    assert rig.wait_for(lambda s: s.uptime_ms > 0, 3)
    start_uptime, start_wall = rig.status.uptime_ms, time.monotonic()

    rig.wait_for(lambda s: False, 1.0)

    elapsed_sim_ms = (time.monotonic() - start_wall) * 1000 * SPEED
    assert rig.status.uptime_ms - start_uptime == pytest.approx(elapsed_sim_ms, rel=0.15)


@pytest.mark.verifies("REQ-023,REQ-002")
def test_lost_plant_link_drives_the_controller_to_fault(rig):
    rig.start_boiler()

    rig.stop_plant()

    assert rig.wait_for(lambda s: s.state == State.FAULT, 3)
    assert raised(rig.status, Fault.TEMP_SENSOR)
    assert rig.status.heater_contactor == 0


@pytest.mark.verifies("REQ-023")
def test_control_waits_for_the_first_inputs_instead_of_faulting(tmp_path):
    rig = Rig(tmp_path, start_plant=False)
    try:
        rig.wait_for(lambda s: False, 1.0)
        assert rig.status.state == State.INIT
        assert rig.status.uptime_ms == 0
        outputs = [f for f in rig.plant_endpoint.receive() if f.kind == FrameKind.OUTPUTS]
        assert outputs
        assert all(BoilerOutputs.from_buffer_copy(f.payload).pump_run == 0 for f in outputs)

        rig.plant_thread.start()

        assert rig.wait_for(lambda s: s.state == State.STANDBY, 3)
        assert rig.status.alarms_latched == 0
    finally:
        rig.close()


@pytest.mark.verifies("REQ-023")
def test_shutdown_writes_safe_outputs(rig):
    rig.start_boiler()
    rig.stop_plant()
    listener = rig.plant_endpoint

    stderr = rig.terminate()

    frames = [f for f in listener.receive() if f.kind == FrameKind.OUTPUTS]
    last = BoilerOutputs.from_buffer_copy(frames[-1].payload)
    assert (last.heater_contactor, last.pump_run, last.valve_open, last.alarm_horn) == (0, 0, 0, 0)
    assert "overruns" in stderr


@pytest.mark.verifies("REQ-022")
def test_stop_wins_over_a_start_in_the_same_interval(rig):
    assert rig.wait_for(lambda s: s.state == State.STANDBY, 3)

    rig.command(start=1, stop=1)
    rig.wait_for(lambda s: False, 0.3)

    assert rig.status.state == State.STANDBY


@pytest.mark.verifies("REQ-040")
def test_fault_injection_from_the_hmi_reaches_the_plant(rig):
    rig.start_boiler()

    rig.hmi.send(
        FrameKind.FAULT_INJECTION, BoilerFaultInjection(fault=PlantFault.PUMP_TRIPPED, action=1)
    )

    assert rig.wait_for(lambda s: raised(s, Fault.PUMP_FAILURE), 3)
    assert "PUMP_TRIPPED" in rig.plant.active_faults()


@pytest.mark.verifies("REQ-024")
def test_watchdog_is_kicked_every_step_and_disarmed_on_exit(tmp_path):
    rig = Rig(tmp_path, watchdog=True)
    try:
        assert rig.wait_for(lambda s: s.uptime_ms >= 2000, 3)
        steps = rig.status.uptime_ms // STEP_MS
        rig.terminate()
    finally:
        rig.close()

    written = rig.watchdog.read_bytes()
    assert written.endswith(b"V")
    assert set(written[:-1]) == {ord("k")}
    assert len(written) - 1 >= steps


def test_runtime_rejects_bad_arguments():
    result = subprocess.run(
        [str(runtime_binary()), "--period-ms", "0"], capture_output=True, text=True, check=False
    )

    assert result.returncode == 2
    assert "usage" in result.stderr
