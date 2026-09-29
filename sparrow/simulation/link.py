"""UDP link between the simulator and the HMI.

Frames use the envelope from sparrow/core/sp_frame.h: a 16-byte header
followed by a struct copied verbatim. Both ends run on little-endian hosts.
"""

from __future__ import annotations

import contextlib
import ctypes
import socket
import struct
from dataclasses import dataclass

MAGIC = 0x57505253
VERSION = 1
HEADER = struct.Struct("<IHHII")
MAX_FRAME = 512


@dataclass
class Frame:
    kind: int
    sequence: int
    payload: bytes


def encode(kind: int, sequence: int, payload: ctypes.Structure) -> bytes:
    body = bytes(payload)
    return HEADER.pack(MAGIC, VERSION, kind, sequence, len(body)) + body


def decode(datagram: bytes) -> Frame | None:
    """Returns the frame, or None if the datagram is not a valid frame."""
    if len(datagram) < HEADER.size:
        return None
    magic, version, kind, sequence, size = HEADER.unpack_from(datagram)
    if magic != MAGIC or version != VERSION or size != len(datagram) - HEADER.size:
        return None
    return Frame(kind, sequence, datagram[HEADER.size :])


class Endpoint:
    """A bound UDP socket that sends frames to one peer and drains received frames."""

    def __init__(
        self, listen_port: int, peer_port: int, host: str = "127.0.0.1", peer_host: str = ""
    ):
        self._peer = (peer_host or host, peer_port)
        self._socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._socket.bind((host, listen_port))
        self._socket.setblocking(False)
        self._sequence = 0

    def send(self, kind: int, payload: ctypes.Structure) -> None:
        self._sequence += 1
        # Best effort: the peer may not be listening yet.
        with contextlib.suppress(OSError):
            self._socket.sendto(encode(kind, self._sequence, payload), self._peer)

    def receive(self) -> list[Frame]:
        frames = []
        while True:
            try:
                datagram, _ = self._socket.recvfrom(MAX_FRAME)
            except (BlockingIOError, ConnectionResetError):
                return frames
            frame = decode(datagram)
            if frame is not None:
                frames.append(frame)

    def close(self) -> None:
        self._socket.close()
