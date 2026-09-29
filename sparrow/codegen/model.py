"""Loading and validation of an application model file (YAML)."""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from pathlib import Path

import yaml

C_TYPES = {
    "f32": ("float", 4),
    "u8": ("uint8_t", 1),
    "u16": ("uint16_t", 2),
    "u32": ("uint32_t", 4),
}
SEVERITIES = ("warning", "critical")
NAME = re.compile(r"^[A-Za-z][A-Za-z0-9_]*$")


class ModelError(Exception):
    pass


@dataclass
class Field:
    name: str
    type: str
    unit: str
    description: str


@dataclass
class Struct:
    name: str
    description: str
    fields: list[Field]


@dataclass
class State:
    name: str
    label: str


@dataclass
class Fault:
    name: str
    severity: str
    text: str
    requirement: str


@dataclass
class Frame:
    name: str
    value: int
    struct: str


@dataclass
class Param:
    name: str
    type: str
    default: float
    minimum: float
    maximum: float
    unit: str
    description: str


@dataclass
class PlantFault:
    name: str
    label: str


@dataclass
class Model:
    application: str
    prefix: str
    outputs: dict[str, Path]
    states: list[State]
    faults: list[Fault]
    structs: list[Struct]
    frames: list[Frame]
    config: list[Param]
    plant_faults: list[PlantFault]
    source: Path = field(default_factory=Path)


def _require(condition: bool, message: str) -> None:
    if not condition:
        raise ModelError(message)


def _unique_names(kind: str, names: list[str]) -> None:
    for name in names:
        _require(bool(NAME.match(name)), f"{kind} name '{name}' is not a valid identifier")
    duplicates = {n for n in names if names.count(n) > 1}
    _require(not duplicates, f"duplicate {kind} name(s): {sorted(duplicates)}")


def _parse_struct(name: str, raw: dict) -> Struct:
    fields = []
    for item in raw["fields"]:
        _require(item["type"] in C_TYPES, f"{name}.{item['name']}: unknown type '{item['type']}'")
        fields.append(
            Field(item["name"], item["type"], item.get("unit", ""), item.get("description", ""))
        )
    _unique_names(f"{name} field", [f.name for f in fields])
    return Struct(name, raw.get("description", "").strip(), fields)


def _parse_param(item: dict) -> Param:
    kind = item.get("type", "f32")
    _require(kind in ("f32", "u32"), f"config {item['name']}: type must be f32 or u32")
    param = Param(
        item["name"],
        kind,
        item["default"],
        item["min"],
        item["max"],
        item.get("unit", ""),
        item.get("description", ""),
    )
    _require(
        param.minimum <= param.default <= param.maximum,
        f"config {param.name}: default {param.default} outside [{param.minimum}, {param.maximum}]",
    )
    return param


def load_model(path: Path) -> Model:
    raw = yaml.safe_load(path.read_text(encoding="utf-8"))
    _require(raw.get("schema") == 1, "unsupported model schema version")
    base = path.parent
    model = Model(
        application=raw["application"],
        prefix=raw["prefix"],
        outputs={key: (base / value).resolve() for key, value in raw["outputs"].items()},
        states=[State(s["name"], s["label"]) for s in raw["states"]],
        faults=[
            Fault(f["name"], f["severity"], f["text"], f.get("requirement", ""))
            for f in raw["faults"]
        ],
        structs=[_parse_struct(name, body) for name, body in raw["structs"].items()],
        frames=[Frame(f["name"], f["value"], f["struct"]) for f in raw["frames"]],
        config=[_parse_param(item) for item in raw["config"]],
        plant_faults=[PlantFault(f["name"], f["label"]) for f in raw["plant_faults"]],
        source=path,
    )
    _validate(model)
    return model


def _validate(model: Model) -> None:
    _unique_names("state", [s.name for s in model.states])
    _unique_names("fault", [f.name for f in model.faults])
    _unique_names("struct", [s.name for s in model.structs])
    _unique_names("config", [p.name for p in model.config])
    _unique_names("plant fault", [f.name for f in model.plant_faults])
    _unique_names("frame", [f.name for f in model.frames])
    _require(len(model.faults) <= 32, "at most 32 faults fit the alarm masks")
    for fault in model.faults:
        _require(fault.severity in SEVERITIES, f"fault {fault.name}: unknown severity")
    struct_names = {s.name for s in model.structs}
    for frame in model.frames:
        _require(frame.struct in struct_names, f"frame {frame.name}: unknown struct {frame.struct}")
    values = [f.value for f in model.frames]
    _require(len(set(values)) == len(values), "frame values must be unique")
    for key in ("c_header", "c_source", "python"):
        _require(key in model.outputs, f"outputs.{key} missing")
