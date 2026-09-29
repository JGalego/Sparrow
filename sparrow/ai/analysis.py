"""Deterministic summary of a recorded simulation trace (the simulator's --csv output).

The model receives this summary, not the raw rows: an hour of 10 Hz data is
36,000 rows, while the events and ranges that matter fit in a few hundred lines.
"""

from __future__ import annotations

import csv
from pathlib import Path


def _decode_mask(mask: int, names: list[str]) -> list[str]:
    return [name for bit, name in enumerate(names) if mask & (1 << bit)]


def summarize_trace(path: Path, states: list[str], faults: list[str]) -> str:
    with path.open(newline="", encoding="utf-8") as handle:
        rows = [{k: float(v) for k, v in row.items()} for row in csv.DictReader(handle)]
    if not rows:
        return "empty trace"
    lines = [f"{len(rows)} samples, t = {rows[0]['time_s']:.1f} .. {rows[-1]['time_s']:.1f} s", ""]
    lines.append("column: min / max / final")
    for column in rows[0]:
        if column in ("time_s", "state") or column.startswith("alarms_"):
            continue
        values = [r[column] for r in rows]
        lines.append(f"  {column}: {min(values):.3f} / {max(values):.3f} / {values[-1]:.3f}")
    lines += ["", "events:"]
    previous = rows[0]
    lines.append(f"  {previous['time_s']:.1f} s  state {states[int(previous['state'])]}")
    for row in rows[1:]:
        if row["state"] != previous["state"]:
            lines.append(
                f"  {row['time_s']:.1f} s  state {states[int(previous['state'])]} -> "
                f"{states[int(row['state'])]} (T = {row.get('temperature_c', float('nan')):.1f})"
            )
        for column in ("alarms_active", "alarms_latched"):
            if column in row and row[column] != previous[column]:
                raised = int(row[column]) & ~int(previous[column])
                cleared = int(previous[column]) & ~int(row[column])
                for name in _decode_mask(raised, faults):
                    lines.append(f"  {row['time_s']:.1f} s  {column} + {name}")
                for name in _decode_mask(cleared, faults):
                    lines.append(f"  {row['time_s']:.1f} s  {column} - {name}")
        previous = row
    return "\n".join(lines)
