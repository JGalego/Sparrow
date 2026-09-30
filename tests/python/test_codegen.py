import ctypes
import shutil
import subprocess
import textwrap

import pytest

from sparrow.codegen import emit_c
from sparrow.codegen.generate import generate
from sparrow.codegen.layout import layout
from sparrow.codegen.model import ModelError, Struct, load_model

MODEL = """
application: demo
schema: 1
prefix: Demo
outputs: {c_header: gen/demo.h, c_source: gen/demo.c, python: gen/demo.py}
states: [{name: IDLE, label: Idle}, {name: RUN, label: Run}]
faults:
  - {name: HOT, severity: critical, text: Too hot}
  - {name: WARM, severity: warning, text: Warm}
structs:
  Sample:
    description: One sample.
    fields:
      - {name: flag, type: u8}
      - {name: value, type: f32, unit: degC}
      - {name: count, type: u16}
frames: [{name: SAMPLE, value: 1, struct: Sample}]
config:
  - {name: limit_c, default: 50.0, min: 0.0, max: 100.0, unit: degC}
  - {name: delay_ms, type: u32, default: 100, min: 0, max: 1000}
plant_faults: [{name: STUCK, label: Stuck}]
"""


def write_model(tmp_path, text=MODEL):
    path = tmp_path / "demo.yaml"
    path.write_text(textwrap.dedent(text))
    return path


def test_layout_makes_padding_explicit_and_matches_ctypes():
    from sparrow.codegen.model import Field

    struct = Struct(
        "Sample",
        "",
        [Field("flag", "u8", "", ""), Field("value", "f32", "", ""), Field("count", "u16", "", "")],
    )

    fields, size = layout(struct)

    class Reference(ctypes.Structure):
        _fields_ = [("flag", ctypes.c_uint8), ("value", ctypes.c_float), ("count", ctypes.c_uint16)]

    assert size == ctypes.sizeof(Reference) == 12
    assert [(f.name, f.offset) for f in fields if not f.is_padding] == [
        ("flag", 0),
        ("value", 4),
        ("count", 8),
    ]
    assert [f.size for f in fields if f.is_padding] == [3, 2]


def test_generated_files_are_written_then_reported_current(tmp_path):
    path = write_model(tmp_path)

    written = generate(path)
    stale = generate(path, check=True)

    assert {p.name for p in written} == {"demo.h", "demo.c", "demo.py"}
    assert stale == []


def test_check_reports_a_hand_edited_file(tmp_path):
    path = write_model(tmp_path)
    generate(path)
    header = tmp_path / "gen" / "demo.h"
    header.write_text(header.read_text() + "/* edit */\n")

    assert generate(path, check=True) == [header]


def test_header_declares_enums_masks_and_size_asserts(tmp_path):
    header = emit_c.header(load_model(write_model(tmp_path)))

    assert "DEMO_STATE_RUN," in header
    assert "#define DEMO_CRITICAL_FAULT_MASK (DEMO_FAULT_BIT(DEMO_FAULT_HOT))" in header
    assert '_Static_assert(sizeof(DemoSample) == 12, "DemoSample layout changed");' in header
    assert "uint32_t delay_ms;" in header


def test_generated_python_matches_the_c_layout(tmp_path):
    path = write_model(tmp_path)
    generate(path)
    namespace: dict = {}
    exec((tmp_path / "gen" / "demo.py").read_text(), namespace)

    assert ctypes.sizeof(namespace["DemoSample"]) == 12
    assert namespace["CONFIG_DEFAULTS"] == {"limit_c": 50.0, "delay_ms": 100}
    assert namespace["PlantFault"].STUCK == 1


@pytest.mark.parametrize(
    ("change", "message"),
    [
        (("default: 50.0", "default: 150.0"), "outside"),
        (("type: u16", "type: i64"), "unknown type"),
        (("struct: Sample", "struct: Missing"), "unknown struct"),
        (("{name: WARM", "{name: HOT"), "duplicate fault"),
        (("severity: warning", "severity: minor"), "unknown severity"),
        (("schema: 1", "schema: 2"), "schema version"),
        (("structs:\n", "structs: []\nunused:\n"), "structs must be a mapping"),
        (("config:\n", "config: []\nunused:\n"), "at least one config parameter"),
        (("{name: RUN, label: Run}", "{name: ON, label: On}"), "quote the name"),
    ],
)
def test_invalid_models_are_rejected(tmp_path, change, message):
    with pytest.raises(ModelError, match=message):
        load_model(write_model(tmp_path, MODEL.replace(*change)))


@pytest.mark.skipif(shutil.which("cc") is None, reason="needs a C compiler")
def test_a_model_without_faults_generates_valid_c_and_python(tmp_path):
    faultless = MODEL.replace(
        MODEL[MODEL.index("faults:") : MODEL.index("structs:")], "faults: []\n"
    )
    path = write_model(tmp_path, faultless)
    generate(path)

    result = subprocess.run(
        ["cc", "-std=c11", "-pedantic-errors", "-Wall", "-fsyntax-only", "gen/demo.c"],
        cwd=tmp_path,
        capture_output=True,
        text=True,
        check=False,
    )

    assert result.returncode == 0, result.stderr
    namespace: dict = {}
    exec((tmp_path / "gen" / "demo.py").read_text(), namespace)
    assert namespace["FAULT_INFO"] == {}
    assert "#define DEMO_CRITICAL_FAULT_MASK (UINT32_C(0))" in (tmp_path / "gen/demo.h").read_text()


def test_a_missing_key_is_reported_with_its_entry(tmp_path):
    with pytest.raises(ModelError, match=r"fault WARM lacks \['text'\]"):
        load_model(write_model(tmp_path, MODEL.replace("text: Warm", "label: Warm")))
