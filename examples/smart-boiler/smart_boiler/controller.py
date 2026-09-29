"""ctypes binding for the C boiler controller (libboiler_controller.so)."""

from __future__ import annotations

import ctypes
from pathlib import Path

from sparrow.simulation.binding import find_library

from .model import (
    BoilerCommands,
    BoilerConfig,
    BoilerInputs,
    BoilerOutputs,
    BoilerStatus,
    default_config,
)


class ControllerInitError(ValueError):
    pass


class BoilerController:
    """One controller instance. The C object is owned here and freed by close()."""

    def __init__(self, config: BoilerConfig | None = None, library: Path | None = None):
        self._lib = ctypes.CDLL(str(library or find_library("boiler_controller")))
        self._declare_signatures()
        status = ctypes.c_int(0)
        config = config or default_config()
        self._handle = self._lib.boiler_capi_create(ctypes.byref(config), ctypes.byref(status))
        if not self._handle:
            raise ControllerInitError(
                f"controller rejected its configuration (status {status.value})"
            )
        self._outputs = BoilerOutputs()
        self._status = BoilerStatus()

    def _declare_signatures(self) -> None:
        lib = self._lib
        lib.boiler_capi_create.restype = ctypes.c_void_p
        lib.boiler_capi_create.argtypes = [
            ctypes.POINTER(BoilerConfig),
            ctypes.POINTER(ctypes.c_int),
        ]
        lib.boiler_capi_destroy.restype = None
        lib.boiler_capi_destroy.argtypes = [ctypes.c_void_p]
        lib.boiler_step.restype = None
        lib.boiler_step.argtypes = [
            ctypes.c_void_p,
            ctypes.POINTER(BoilerInputs),
            ctypes.POINTER(BoilerCommands),
            ctypes.c_uint32,
            ctypes.POINTER(BoilerOutputs),
        ]
        lib.boiler_get_status.restype = None
        lib.boiler_get_status.argtypes = [ctypes.c_void_p, ctypes.POINTER(BoilerStatus)]

    def step(self, inputs: BoilerInputs, commands: BoilerCommands, dt_ms: int) -> BoilerOutputs:
        self._lib.boiler_step(
            self._handle,
            ctypes.byref(inputs),
            ctypes.byref(commands),
            dt_ms,
            ctypes.byref(self._outputs),
        )
        return self._outputs

    def status(self) -> BoilerStatus:
        self._lib.boiler_get_status(self._handle, ctypes.byref(self._status))
        return self._status

    def close(self) -> None:
        if self._handle:
            self._lib.boiler_capi_destroy(self._handle)
            self._handle = None

    def __enter__(self) -> BoilerController:
        return self

    def __exit__(self, *exc: object) -> None:
        self.close()

    def __del__(self) -> None:
        self.close()
