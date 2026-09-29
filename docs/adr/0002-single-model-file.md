# 2. One model file defines the shared interfaces

Status: accepted

## Context

The controller (C), the plant simulation (Python) and the HMI (C) exchange the same structures: inputs, outputs, commands, status, faults, states and configuration. Written by hand in two languages, they drift apart, and the drift shows up only at run time.

## Decision

`model/boiler.yaml` is the only definition. `sparrow gen` writes the C header and source and a Python ctypes module from it. Struct padding is made explicit, and both sides assert the struct size. The generated files are committed so that the C build needs no Python, and `make lint` fails when they are stale.

## Consequences

- A field added to the model reaches every component in one step.
- Configuration defaults and allowed ranges have one source. The range check in the controller is generated.
- Requirements reference config parameters by name, and `sparrow trace` checks that the names exist.
