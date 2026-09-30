"""ctypes binding for the C landing gear controller (libgear_controller.so)."""

from __future__ import annotations

import ctypes
from pathlib import Path

from sparrow.simulation.binding import find_library

from .model import GearCommands, GearConfig, GearInputs, GearOutputs, GearStatus, default_config


class ControllerInitError(ValueError):
    pass


class GearController:
    """One controller instance. The C object is owned here and freed by close()."""

    def __init__(self, config: GearConfig | None = None, library: Path | None = None):
        self._lib = ctypes.CDLL(str(library or find_library("gear_controller")))
        self._declare_signatures()
        status = ctypes.c_int(0)
        config = config or default_config()
        self._handle = self._lib.gear_capi_create(ctypes.byref(config), ctypes.byref(status))
        if not self._handle:
            raise ControllerInitError(
                f"controller rejected its configuration (status {status.value})"
            )
        self._outputs = GearOutputs()
        self._status = GearStatus()

    def _declare_signatures(self) -> None:
        lib = self._lib
        lib.gear_capi_create.restype = ctypes.c_void_p
        lib.gear_capi_create.argtypes = [ctypes.POINTER(GearConfig), ctypes.POINTER(ctypes.c_int)]
        lib.gear_capi_destroy.restype = None
        lib.gear_capi_destroy.argtypes = [ctypes.c_void_p]
        lib.gear_step.restype = None
        lib.gear_step.argtypes = [
            ctypes.c_void_p,
            ctypes.POINTER(GearInputs),
            ctypes.POINTER(GearCommands),
            ctypes.c_uint32,
            ctypes.POINTER(GearOutputs),
        ]
        lib.gear_get_status.restype = None
        lib.gear_get_status.argtypes = [ctypes.c_void_p, ctypes.POINTER(GearStatus)]

    def step(self, inputs: GearInputs, commands: GearCommands, dt_ms: int) -> GearOutputs:
        self._lib.gear_step(
            self._handle,
            ctypes.byref(inputs),
            ctypes.byref(commands),
            dt_ms,
            ctypes.byref(self._outputs),
        )
        return self._outputs

    def status(self) -> GearStatus:
        self._lib.gear_get_status(self._handle, ctypes.byref(self._status))
        return self._status

    def close(self) -> None:
        if self._handle:
            self._lib.gear_capi_destroy(self._handle)
            self._handle = None

    def __enter__(self) -> GearController:
        return self

    def __exit__(self, *exc: object) -> None:
        self.close()

    def __del__(self) -> None:
        self.close()
