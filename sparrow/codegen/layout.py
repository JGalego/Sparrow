"""Struct layout shared by the C and Python emitters."""

from __future__ import annotations

from dataclasses import dataclass

from .model import C_TYPES, Struct


@dataclass
class LaidOutField:
    name: str
    type: str
    offset: int
    unit: str
    description: str
    is_padding: bool = False
    size: int = 0


def _align(offset: int, alignment: int) -> int:
    return (offset + alignment - 1) // alignment * alignment


def layout(struct: Struct) -> tuple[list[LaidOutField], int]:
    """Returns fields with explicit padding and the total size.

    Padding follows natural alignment, so the result matches what a C compiler
    and ctypes both produce. Making it explicit lets a reader see the wire
    layout and keeps it stable if a field is reordered.
    """
    fields: list[LaidOutField] = []
    offset = 0
    pads = 0
    largest = 1

    def pad_to(target: int) -> None:
        nonlocal offset, pads
        if target > offset:
            fields.append(
                LaidOutField(f"reserved{pads}", "u8", offset, "", "", True, target - offset)
            )
            pads += 1
            offset = target

    for item in struct.fields:
        size = C_TYPES[item.type][1]
        largest = max(largest, size)
        pad_to(_align(offset, size))
        fields.append(LaidOutField(item.name, item.type, offset, item.unit, item.description))
        offset += size
    pad_to(_align(offset, largest))
    return fields, offset
