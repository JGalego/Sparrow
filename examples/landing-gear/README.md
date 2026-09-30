# Landing gear

A controller for a tricycle landing gear (nose, left main, right main) driven by one hydraulic system, with gravity free-fall as the alternate extension. It retracts the gear only in the air, extends it to three green, reports a gear disagree when a transit does not finish, lets the crew free-fall the gear after a hydraulic failure, and sounds the "too low, gear" horn on an approach with the gear up.

The requirements, the model's states, faults and limits, the controller and its tests were written by `sparrow ai` with `gpt-6.1-sol` on 2026-09-30, one requirement at a time, and reviewed by a person. This page records every command, its output and every human edit. The plumbing that no `sparrow ai` stage writes (the aircraft interface, the plant simulation, the build wiring) was written by hand and is listed [below](#written-by-hand).

| Stage | Runs | Time | Tokens in / out | Result |
|---|---|---|---|---|
| 1 requirements | 1 | 57 s | 2,284 / 3,194 | 13 requirements, used as written |
| 2 model | 6 (+1 rejected reply) | 112 s | 40,440 / 5,843 | 2 human edits |
| 4 code | 13 | 501 s | 95,012 / 26,701 | correct; reference paths fixed |
| 5 tests | 13 | 1,923 s | 182,880 / 98,417 | all gates passed first time |
| 7 analyze | 1 | 25 s | 2,532 / 1,234 | missed a nuisance horn |
| change: requirements, code, tests | 4 | 255 s | 43,699 / 13,969 | REQ-014 added; 11 old tests broke |
| 5 explain | 1 | 45 s | 27,497 / 2,625 | all 11 diagnoses correct |
| 1 requirements, 5 tests (repair) | 5 | 454 s | 77,373 / 22,745 | all gates pass |
| review | 1 | 31 s | 80,787 / 1,327 | 4 valid findings |
| 5 tests (closed loop) | 2 | 368 s | 54,627 / 18,453 | 6 closed-loop tests |

Total: 49 requests, 607,131 tokens in and 194,508 out, $3.16 at $2/$10 per million, and 63 minutes, including the gate runs of `--verify`.

The result: 14 requirements, a 260-line controller, 61 C unit tests and 6 closed-loop tests, all passing, and every requirement traced to both. The gear logic and every test that cites a requirement came from the model; what remains of the hand-written skeleton is the configuration check, `gear_init`, the saturating time counter and the status copy. No AI output in the controller or its tests was edited by hand, apart from the `make format` gate.

## Run it

From the repository root:

```sh
make setup                    # once, or again after pulling: installs the landing_gear package
source .venv/bin/activate     # once per shell: python, sparrow, pytest on the PATH
make build
```

Fly a scenario. The run is not paced in real time and prints what the crew would see and hear:

```sh
python -m landing_gear hydraulic-failure
python -m landing_gear approach --csv run.csv     # also record the full trace
```

| Scenario | What happens |
|---|---|
| `takeoff` | Rotation at 140 kt, gear up three seconds after liftoff |
| `approach` | Gear down at 900 ft, three green, touchdown |
| `gear-not-down` | The crew forgets the gear; the horn sounds below 500 ft |
| `hydraulic-failure` | The pump fails, the gear stays up, gear disagree, free-fall to three green |
| `squat-switch-failure` | Both squat switches fail to "air" while taxiing; the airspeed interlock keeps the gear down |
| `nose-gear-jammed` | The mains lock, the nose does not: two green and a latched disagree |

Tests and traceability:

```sh
make test                                          # everything, this example included
ctest --preset host -R test_gear                   # this example's C tests
pytest examples/landing-gear/tests/python          # its plant and closed-loop tests
sparrow trace examples/landing-gear/sparrow.yaml --requirement REQ-009
```

The matrix is in [traceability.md](traceability.md). There is no graphical HMI; the frames for one (`STATUS`, `COMMANDS`) are in the model.

## Layout

| Path | Contents | Written by |
|---|---|---|
| [model/gear.yaml](model/gear.yaml) | Interface structs, states, faults, config | person (interface), `sparrow ai model` (the rest) |
| [requirements/controller.yaml](requirements/controller.yaml) | REQ-001 to REQ-014 | `sparrow ai requirements`, `code` (implementation lists) |
| [controller/](controller/) | `gear_controller.c/.h`; `gear_capi.c` for ctypes | `sparrow ai code`; person (skeleton, `gear_capi`) |
| [generated/](generated/), [landing_gear/model.py](landing_gear/model.py) | Interface code | `sparrow gen` |
| [tests/c/](tests/c/) | Unit tests; `gear_fixture.h` | `sparrow ai tests`; person (fixture, extended by AI) |
| [tests/python/](tests/python/) | Closed-loop tests; plant tests, `conftest.py` | `sparrow ai tests`; person |
| [landing_gear/](landing_gear/) | Plant, bench, ctypes binding, scenario runner | person |
| [scenarios/](scenarios/) | Six scenarios | person |

## Written by hand

No `sparrow ai` stage writes a project from nothing: the requirements stage needs requirement files and a model to add to, the code stage needs a controller to change, and nothing writes the plant. Before the first request the example had:

- **The aircraft interface** in the model: `Inputs` (hydraulic pressure transmitter, airspeed and radio altitude with validity, gear lever, alternate extension handle, six lock sensors, two squat switches), `Outputs` (two selector valves, uplock release, lever lock, gear horn, master warning), `Commands` (mute, reset) and a minimal `Status`. One state, `INIT`; no faults; one config parameter, the transmitter span. These are fixed by the installation, not by the requirements.
- **A controller skeleton**: `gear_init`, `gear_step`, `gear_get_status` that validate the configuration, count time and keep every output off. The ctypes wrapper `gear_capi.c`.
- **The plant** ([landing_gear/plant.py](landing_gear/plant.py)): hydraulic pressure with a first-order lag, three legs with uplocks, downlocks, 7 s extension, 8 s retraction and 12 s free-fall, an aircraft with airspeed, height, rotation and touchdown, a radio altimeter that reads to 2500 ft, squat switches, and a lever the lever lock holds down. Faults: pump failure, jammed nose gear, squat switches stuck in air, radio altimeter failure.
- **The bench, the scenario runner, six scenarios, the C fixture, `conftest.py` and 10 plant tests.**
- **Wiring**: CMake (the controller sources are a glob, so files the code stage adds are built), pytest paths, and the gates in the Makefile (`gen`, `format`, `trace`, `lint`, `test` cover both examples).

On the skeleton every gate passed, and the approach ended on the runway with the gear up:

```text
$ python -m landing_gear approach
  time    alt  kias    hyd  N L R  controller    event
  0.00   1000   150   3000  - - -  Initializing  start
  7.00    895   150   3000  - - -  Initializing  pilot: gear lever DOWN
 66.70      0   150   3000  - - -  Initializing  TOUCHDOWN WITH GEAR NOT LOCKED
 67.00      0   150   3000  - - -  Initializing  pilot: decelerating 4 kt/s
```

`N L R` are the gear lights: `G` down and locked, `R` in transit, `-` up.

## Walkthrough

Setup, as in the [Smart Boiler walkthrough](../../docs/openai-walkthrough.md): the `openai` profile in `sparrow-ai.local.yaml`, the key in `.env`. Every command ran from the repository root with:

```sh
export SPARROW_PROJECT=examples/landing-gear/sparrow.yaml
export SPARROW_AI_EFFORT=high      # code and tests stages only
```

```text
$ sparrow ai ping
openai gpt-6.1-sol: 'ready' (21 in, 4 out)
```

Outputs are quoted as printed, with the streaming progress dots removed. Each `saved .sparrow/...` line names the proposal the run wrote; those are ignored by Git and are omitted below after the first.

### 1. Requirements

The idea described six behaviours in the crew's terms:

```text
$ sparrow ai requirements "Landing gear control for a tricycle gear (nose, left main, right main)
  driven by one hydraulic system. The controller starts in INIT and must pick up the gear where it
  is. (1) Ground retraction interlock: never retract with weight on either main wheel, below a
  minimum airspeed, or without a valid airspeed; hold the gear lever down with the lever lock
  solenoid while on the ground. (2) Normal extension to all three legs down and locked (three
  greens). (3) Normal retraction to all three legs up and locked; if the lever goes down during
  retraction, extend at once. (4) Gear disagree: a transit that does not complete in time latches
  a critical fault and lights the master warning, and the controller keeps driving the gear toward
  the lever; a reset is accepted only once the gear agrees with the lever. (5) Alternate
  extension: while the alternate extension handle is pulled, fire the uplock release and let the
  gear free-fall, whatever the lever position and hydraulic pressure. (6) Too low, gear: below 500
  ft radio altitude and 190 kt with the gear not down and locked, sound the gear horn; the crew can
  mute it above 200 ft but not below." --apply
request 1: gpt-6.1-sol, 2284 tokens in, 3194 out
saved .sparrow/proposals/20260930-001047-requirements.patch
saved .sparrow/proposals/20260930-001047-requirements.json
applied to the working tree; review with `git diff`
# Add controller requirements REQ-001 through REQ-013 covering initialization, interlocks, normal
# and alternate operation, gear disagree, and the gear warning horn.
#
# All requirements belong in controller.yaml. Implementation fields remain empty because no
# implementation was supplied. The model task must add configuration parameters for the minimum
# retraction airspeed (kt), normal transit timeout (ms), gear-warning radio-altitude threshold
# (500 ft), gear-warning airspeed threshold (190 kt), and horn-mute minimum radio altitude
# (200 ft). No parameter names or unspecified defaults are invented here; parameters lists remain
# empty until those model entries exist. The requirements make threshold equality,
# alternate-extension priority, fault recovery, and mute cancellation explicit.
+  - id: REQ-007
+    title: Normal transit timeout
+    statement: >-
+      The controller shall start a normal transit timer when it first
+      energizes a hydraulic selector valve toward an incomplete lever-selected
+      position. If that position remains incomplete when elapsed transit time
+      reaches or exceeds the configured normal transit timeout, it shall assert
+      and latch a critical gear-disagree fault in that control step. ...
```

The diff (248 lines, abridged here to one requirement) split six behaviours into 13 requirements:

| ID | Title |
|---|---|
| REQ-001 | Initialize from sensed gear position |
| REQ-002 | Ground lever lock |
| REQ-003 | Retraction interlock |
| REQ-004 | Normal extension and three greens |
| REQ-005 | Normal retraction and immediate reversal |
| REQ-006 | Hydraulic selector exclusion |
| REQ-007 | Normal transit timeout |
| REQ-008 | Gear-disagree fault operation and reset |
| REQ-009 | Alternate extension priority |
| REQ-010 | Too-low gear warning condition |
| REQ-011 | Gear horn and permitted mute |
| REQ-012 | Unmutable warning at low altitude |
| REQ-013 | Gear warning termination and rearming |

None was asked for by name; each one pins down something the idea left open: what "down and locked" means when a leg reports both locks (REQ-001), that the two valves are never energized together (REQ-006), when the transit timer starts and what resets it (REQ-007), and that a mute belongs to one warning episode (REQ-013). It chose some behaviour the idea did not state: alternate extension turns both hydraulic valves off (REQ-009), and MUTE does not put out the master warning (REQ-008). Both were kept. It named no parameters, as instructed, and listed the five the model would need. The requirements were used as written.

### 2. Model

One run per requirement that needs a state, a fault or a limit:

```text
$ sparrow ai model REQ-001 --apply
request 1: gpt-6.1-sol, 4652 tokens in, 626 out
applied to the working tree; review with `git diff`
# Add model states for sensed lock completion and startup-selected normal or alternate operation.
+  # REQ-001: select operation from current lock inputs, not a prior position.
+  - {name: DOWN_LOCKED, label: Down and locked}
+  - {name: UP_LOCKED, label: Up and locked}
+  - {name: EXTENDING, label: Extending}
+  # Retraction selection does not imply that the interlock permits valve drive.
+  - {name: RETRACTING, label: Retraction selected}
+  - {name: ALTERNATE_EXTENDING, label: Alternate extension}

$ sparrow ai model REQ-003 --apply
request 1: gpt-6.1-sol, 4754 tokens in, 537 out
# Add the minimum retraction airspeed configuration required by REQ-003.
+  - {name: min_retraction_airspeed_kt, default: 80.0, min: 40.0, max: 200.0, unit: kt, ...}
```

The first `model REQ-007` wrote its fault with `label:` where the schema has `text:`. Sparrow applied it, and from then on every command stopped with a Python traceback:

```text
$ sparrow ai model REQ-007 --apply
request 1: gpt-6.1-sol, 4894 tokens in, 735 out
applied to the working tree; review with `git diff`
+  - {name: GEAR_DISAGREE, label: Gear disagree, severity: critical}

$ sparrow ai model REQ-008 --apply
Traceback (most recent call last):
  ...
  File "/home/jgalego/git/Sparrow/sparrow/codegen/model.py", line 149, in load_model
    Fault(f["name"], f["severity"], f["text"], f.get("requirement", ""))
KeyError: 'text'
```

Two Sparrow fixes followed: the model loader names a missing key instead of raising `KeyError`, and `sparrow ai` loads a proposed model (or requirement file) before applying it, so an invalid one is sent back as a rejected reply. The change was reverted and the stage run again:

```text
$ sparrow ai model REQ-007 --apply
note: attempt 1 rejected: examples/landing-gear/model/gear.yaml: invalid model: fault GEAR_DISAGREE lacks ['text']
request 1: gpt-6.1-sol, 4894 tokens in, 820 out
request 2: gpt-6.1-sol, 5529 tokens in, 608 out
applied to the working tree; review with `git diff`
# Add a critical gear-disagree fault, a fault state, and a configurable normal transit timeout for REQ-007.
#
# The fault includes the required text field and cites REQ-007. ...
+  - {name: FAULT, label: Fault}
+  - {name: GEAR_DISAGREE, label: Gear disagree, text: "Normal hydraulic transit did not complete before its timeout", severity: critical}
+  - {name: normal_transit_timeout_ms, default: 15000.0, min: 1000.0, max: 120000.0, unit: ms, ...}
```

`model REQ-008` added no names and documented the reset and master-warning contract on `FAULT`, `mute` and `reset`. `model REQ-010` added the `TOO_LOW_GEAR` warning and `gear_warning_altitude_ft` (500) and `gear_warning_airspeed_kt` (190); `model REQ-011` added `horn_mute_min_altitude_ft` (200). Each took 13 to 17 s.

Human edits, after reading the combined diff:

- `requirement: REQ-007` and `requirement: REQ-010` on the two faults. The stage is told to cite the requirement on new faults, and the rationale above says it did; neither diff does.
- `normal_transit_timeout_ms` as `type: u32` with integer limits, like every other `_ms` parameter in the repository. The model wrote it as a float.

### 4. Code

One run per requirement, in order, each building on the last:

```sh
for id in REQ-001 REQ-002 ... REQ-013; do sparrow ai code $id --apply; done
```

Every reply was accepted on the first attempt:

| | Time | Tokens in / out | The model's summary |
|---|---|---|---|
| REQ-001 | 52 s | 5,915 / 2,818 | Implement sensed-position startup selection, including contradictory-lock rejection, alternate-extension priority, and the retraction interlock. |
| REQ-002 | 18 s | 6,372 / 916 | Implement REQ-002 by updating handle_lock from the weight-on-wheels inputs each control step. |
| REQ-003 | 37 s | 6,524 / 1,728 | Apply the retraction interlock at the control-step output boundary and complete REQ-003 traceability. |
| REQ-004 | 29 s | 6,544 / 1,658 | Extract explicit normal-extension selection and populate REQ-004 implementation references. |
| REQ-005 | 25 s | 6,694 / 1,541 | Add an explicit normal-retraction selector for REQ-005 and populate its implementation references. |
| REQ-006 | 40 s | 6,751 / 2,045 | Centralize hydraulic selector commands so both valves are cleared before at most one is energized. |
| REQ-007 | 77 s | 7,098 / 4,389 | Implement REQ-007 with a deterministic normal-transit timer, latched gear-disagree fault, and requirement traceability. |
| REQ-008 | 46 s | 7,685 / 2,242 | Implement agreement-gated gear-disagree reset while preserving fault-state hydraulic operation, interlocks, alternate extension, and master warning behavior. |
| REQ-009 | 38 s | 7,912 / 1,934 | Make alternate-extension priority explicit in a dedicated controller helper. |
| REQ-010 | 48 s | 8,101 / 2,545 | Implement REQ-010 as a non-latched warning condition using the existing configuration limits. |
| REQ-011 | 31 s | 8,318 / 1,832 | Implement REQ-011 with immediate, episode-specific horn muting using horn_mute_min_altitude_ft. |
| REQ-012 | 34 s | 8,531 / 1,779 | Make low-altitude mute cancellation explicit and complete REQ-012 implementation traceability. |
| REQ-013 | 26 s | 8,567 / 1,274 | Make REQ-013 warning termination explicit and add implementation traceability. |

The result is [controller/gear_controller.c](controller/gear_controller.c). Each step selects the operation afresh from the lock sensors; alternate extension is checked first; a single selector function clears both valves before energizing one; the retraction gate is applied after selection, so it holds in every state including `FAULT`; the transit timer is separate from the state timer, so an inhibited retraction does not pause it. It compiled without a warning under `-Wall -Wextra -Wpedantic -Wconversion`.

Two problems, one found at review and one by `sparrow trace`:

- The model's `mute` description, taken from the hand-written interface, promised that MUTE acknowledges all alarms. No requirement asks for it and the code never does. It was left for the review stage to judge (see [Review](#review)).
- The implementation references were relative to the repository root (`examples/landing-gear/controller/gear_controller.c::gear_step`), but `sparrow trace` resolves them against `sparrow.yaml`, so every one was "file not found". In the Smart Boiler the existing references showed the convention; here there were none, and the instruction did not state it. The prefix was removed by hand, and the code task now says which directory the paths are relative to.

On the finished controller, before any test was written, the approach scenario landed with three green.

### 5. Tests

`--verify` runs `make trace`, which fails while any requirement lacks a test, so on a new project the first twelve runs would fail on the requirements not yet tested. The thirteen runs used `--apply`, and the gates ran once at the end.

```sh
for id in REQ-001 ... REQ-013; do sparrow ai tests $id --apply; done
```

All thirteen were accepted on the first attempt, 84 to 215 s each, 1,923 s in all. They wrote 11 test files and registered each in the test CMakeLists. Seven of the thirteen replies said the same thing, first at REQ-003:

```text
# Add and register C tests for configured retraction-speed boundaries, same-step permission loss,
# recovery, and interlock enforcement during a latched fault. Python closed-loop tests require
# additional GearBench API context.
```

The tests task received `conftest.py` but not the bench it imports, so no closed-loop test could be written without guessing an API. This was not acted on until the review raised it.

The gates, in the order `--verify` runs them:

```text
$ make gen && make format && make trace && make lint && make test
...
100% tests passed, 0 tests failed out of 24
============================= 177 passed in 25.93s =============================
traceability with results: build/host/traceability.md, build/host/landing-gear-traceability.md
```

Every requirement had passing tests, and `sparrow trace --results` confirmed that each test that cites one was built and ran. To see whether the tests would notice a wrong controller, five defects were planted by hand, one at a time:

| Planted defect | Suites failing (of 11) |
|---|---|
| Minimum retraction airspeed: `>=` becomes `>` | 8 |
| Only the left squat switch inhibits retraction | 4 |
| Horn mute floor: `<=` becomes `<` | 2 |
| Alternate extension leaves the down valve energized | 3 |
| Transit timeout: `>=` becomes `>` | 9 |

All five were caught.

### 7. Simulation

The six scenarios against the controller:

```text
$ python -m landing_gear takeoff
scenario takeoff: Take-off roll from 100 kt, rotation at 140 kt. The crew calls "positive rate,
gear up" three seconds after liftoff and the gear retracts.

  time    alt  kias    hyd  N L R  controller      event
  0.00      0   100   3000  G G G  Initializing    start
  0.05      0   100   3000  G G G  Down and locked controller: Down and locked
 13.35      2   140   3000  G G G  Down and locked liftoff
 17.00    123   151   3000  G G G  Down and locked pilot: gear lever UP
 17.00    123   151   3000  G G G  Down and locked pilot: accelerating 1 kt/s
 17.05    125   151   3000  R R R  Retraction selected controller: Retraction selected
 17.05    125   151   3000  R R R  Retraction selected gear lights R R R
 17.10    127   151   3000  R R R  Retraction selected caution: Valid low altitude and low airspeed with gear not down and locked
 17.10    127   151   3000  R R R  Retraction selected gear horn ON
 25.05    392   159   3000  - - -  Retraction selected gear lights - - -
 25.10    393   159   3000  - - -  Up and locked   controller: Up and locked
 28.35    502   162   3000  - - -  Up and locked   clears: Valid low altitude and low airspeed with gear not down and locked
 28.35    502   162   3000  - - -  Up and locked   gear horn off

end at 35.0 s: Up and locked, lights - - -, alarms: none
```

The gear horn sounded on every take-off, from gear up until the aircraft passed 500 ft. The controller did what REQ-010 says, and REQ-010 says what the idea said: below 500 ft and 190 kt with the gear not down and locked. The defect was in the idea. Real aircraft arm this warning from throttle or flap position, which this interface does not have. The other five scenarios behaved as intended; their final runs are [at the end](#final-runs).

Before fixing it, the recorded trace went to the analysis stage without a requirement to focus on:

```text
$ python -m landing_gear takeoff --csv build/runs/takeoff.csv
$ sparrow ai analyze build/runs/takeoff.csv --scenario examples/landing-gear/scenarios/takeoff.yaml
request 1: gpt-6.1-sol, 2532 tokens in, 1234 out
The reported state and alarm sequence is consistent with the takeoff scenario and requirements. No
definite violation, oscillation, or overshoot is evident. Exact same-step behavior, lock-sensor
predicates, and timeout compliance cannot be established from this summary alone.

WARNING takeoff.csv: available summary and coverage [REQ-002, REQ-003, REQ-004, REQ-006, REQ-008,
REQ-009, REQ-010, REQ-011, REQ-012, REQ-013]: The summary is insufficient for full compliance
checking. down_valve remaining zero establishes selector exclusion for this run, and uplock_release
remaining zero is consistent with no alternate-extension request. However, handle_lock extrema do
not establish its alignment with both WOW inputs, and gear_horn extrema do not establish its
alignment with the warning. This run does not demonstrate extension, reversal, interlock loss,
fault/reset handling, alternate extension, or mute behavior, and it does not supply the samples
needed to check threshold boundaries.
INFO    takeoff.csv: 0.1 s, DOWN_LOCKED [REQ-001]: The first reported state is consistent with
initialization from gear that is down and locked and a down lever. However, position values alone
do not establish the required lock-sensor predicate. The summary also cannot verify the initial
INIT state or de-energized outputs before the first control step.
INFO    takeoff.csv: 17.1 s, DOWN_LOCKED -> RETRACTING [REQ-003, REQ-005]: Retraction follows the
scheduled gear-up command at 17 s. The plant settings imply liftoff near 13.3 s and approximately
151 kt at the command, so the timing is plausible. The displayed 0.1 s difference does not by
itself demonstrate slow detection: sample rounding and command/control ordering are not supplied.
Permission at this step still requires both WOW inputs clear, valid airspeed, and the configured
minimum retraction speed.
INFO    takeoff.csv: TOO_LOW_GEAR active from 17.1 s to 28.4 s [REQ-010, REQ-011, REQ-012,
REQ-013]: This warning during takeoff is expected under the stated requirements, not evidence of a
malfunction. At retraction initiation, the scenario implies altitude near 125 ft and speed near
151 kt. Once the down-and-locked predicate is lost, valid inputs at those values require the
warning and an unmutable horn. Reaching UP_LOCKED at 25.1 s must not clear the warning because the
gear is still not down and locked. At 2000 ft/min, the aircraft should cross 500 ft near 28.3-28.4
s, explaining warning termination at 28.4 s; speed remains below 190 kt. Actual validity, lock
inputs, altitude samples, and horn edges are needed to verify exact same-step activation and
termination.
INFO    takeoff.csv: 25.1 s, RETRACTING -> UP_LOCKED [REQ-005, REQ-007, REQ-008]: The reported
retraction takes approximately 8.0 s and completes without a reported gear-disagree alarm or master
warning. There are no repeated state transitions suggesting oscillation. Whether 8.0 s is
acceptable depends on the configured normal transit timeout and actual timer start. Completion on
the timeout step must take precedence. The final up_valve value of zero is consistent with
completed retraction, but does not prove it switched off on the completion step.

Next steps:
  - Inspect raw samples around 17 s, 25.1 s, and 28.4 s, including command timing, both WOW inputs,
    all lock inputs, validity inputs, radio altitude, valve outputs, horn, and alarms.
  - Obtain the configured minimum retraction airspeed and normal transit timeout before judging
    interlock permission or the 8.0 s transit duration.
  - Use requirement-linked tests for each limit just below, at, and just above its boundary, plus
    separate scenarios for reversal, permission loss, fault/reset, alternate extension, and mute
    cancellation/rearming.
```

It saw the warning, timed it correctly, and judged it against the requirements, which it met. Whether a horn on every take-off is acceptable is not in the requirements; it took a person to call it a defect.

### A requirement change: arming the warning

The fix was chosen by a person and uses only existing inputs: the warning arms once a valid radio altitude at or above 500 ft has been seen since the squat switches were last made. A forgotten gear on approach still sounds the horn; the initial climb does not.

```text
$ sparrow ai requirements "The too-low gear warning (REQ-010) sounds on every normal take-off: the
  gear retracts below 500 ft and 190 kt, so the horn comes on during the initial climb until the
  aircraft passes 500 ft. That is a nuisance warning. Amend REQ-010 so the warning is armed only
  after a valid radio altitude at or above the warning altitude has been seen since either
  weight-on-wheels switch was last made; it is disarmed while on the ground and stays disarmed
  through the initial climb. A forgotten gear on approach must still sound the horn. Do not add new
  inputs." --apply
request 1: gpt-6.1-sol, 6231 tokens in, 1741 out
applied to the working tree; review with `git diff`
# Amend REQ-010 to require warning arming and add REQ-014 defining ground reset and altitude-based
# arming without new inputs or configuration.

$ sparrow ai code REQ-014 --apply
request 1: gpt-6.1-sol, 9097 tokens in, 1767 out
applied to the working tree; review with `git diff`
# Implement REQ-014 with a per-controller warning-arming latch, ground-priority disarming, and
# same-step warning evaluation.
+static void gear_update_warning_arming(GearController *c, const GearInputs *inputs)
+{
+    /* Ground indication clears the qualification even at or above the altitude limit. */
+    if (inputs->left_wow != 0 || inputs->right_wow != 0) {
+        c->gear_warning_armed = 0;
+    } else if (inputs->radio_altitude_valid != 0 &&
+               inputs->radio_altitude_ft >= c->config.gear_warning_altitude_ft) {
+        c->gear_warning_armed = 1;
+    }
+}

$ sparrow ai code REQ-010 --apply
request 1: gpt-6.1-sol, 17490 tokens in, 1027 out
applied to the working tree; review with `git diff`
# Add REQ-010 implementation traceability; retain the already-compliant controller behavior.
#
# ... No controller change is needed. Some supplied tests expect a warning after initialization
# without REQ-014 altitude qualification; those expectations need separate test updates outside
# the permitted scope.
```

The last reply predicted what the tests run found. Now that every requirement had tests, `--verify` was meaningful:

```text
$ sparrow ai tests REQ-014 --verify
request 1: gpt-6.1-sol, 10881 tokens in, 9434 out
applied to the working tree; review with `git diff`
gate pass: make gen
gate pass: make format
gate pass: make trace
gate pass: make lint
gate FAIL: make test
FAIL test_gear_too_low_warning.warning_thresholds_are_strict_and_clear_on_crossing  .../test_gear_too_low_warning.c:30: expected == (f.status.alarms_active & warning) != 0 (expected 1, got 0)
FAIL test_gear_too_low_warning.warning_requires_both_validity_inputs_and_recovers  .../test_gear_too_low_warning.c:61: 1 == (f.status.alarms_active & warning) != 0 (expected 1, got 0)
FAIL test_gear_horn_mute.horn_initialization_clears_previous_episode_mute  .../test_gear_horn_mute.c:32: ...
  (8 horn tests and 1 initialization test in all)
88% tests passed, 3 tests failed out of 25
```

The new REQ-014 suite passed; 11 older tests expected a warning straight after initialization.

```text
$ sparrow ai explain
request 1: gpt-6.1-sol, 27497 tokens in, 2625 out
All 11 reported failures come from tests expecting a too-low gear warning without first
establishing the arming history required by REQ-014. The shown controller initializes disarmed,
updates arming before evaluating the warning, and requires that arming state for warning assertion.
These are test defects against the current requirements, not evidence that the warning or horn
implementation is wrong. The reinitialization test also incorrectly expects arming to survive
gear_init.

ERROR   .../test_gear_horn_mute.c::horn_initialization_clears_previous_episode_mute (reported line
32) [REQ-011,REQ-014]: The test is wrong. horn_set_warning_inputs supplies
horn_mute_min_altitude_ft + 1.0f, which the test explicitly asserts is below
gear_warning_altitude_ft. It never takes a qualifying altitude step before expecting alarms_active
to contain the warning. REQ-014 says initialization starts disarmed; gear_init uses
memset(controller, 0, sizeof *controller), and gear_update_too_low_warning requires
c->gear_warning_armed != 0. Fix by taking an airborne, valid-altitude step at or above the warning
threshold before beginning the muted episode. ...
  (one ERROR per failing test, 11 in all, each placing the defect in the test)
WARNING examples/landing-gear/requirements/controller.yaml::REQ-012 [REQ-012]: There is a separate
wording ambiguity, not the cause of these failures. The statement specifies cancellation 'at or
below 200 ft' and climbing 'above 200 ft', while its rationale and parameters identify
horn_mute_min_altitude_ft. ... Clarify the statement to name horn_mute_min_altitude_ft with 200 ft
as its default, consistent with REQ-011.
INFO    examples/landing-gear/controller/gear_controller.c::gear_step [REQ-010,REQ-011,REQ-014]: The
shown implementation has the required evaluation order: gear_update_warning_arming, then
gear_update_too_low_warning, then gear_update_horn. Do not remove the armed predicate or initialize
armed merely to satisfy these legacy expectations. ...
```

Every diagnosis was correct, and it warned against the easy wrong fix: arming the controller at start-up to make the old tests pass. The Smart Boiler walkthrough applied its `explain` findings by hand; here the tests task was changed instead, so that it receives the failing results of the tests that cite its requirement and is asked to correct them in place. REQ-012 was clarified first:

```text
$ sparrow ai requirements "REQ-012 states its limit as a fixed 200 ft ('at or below 200 ft', 'above
  200 ft'), while REQ-011 and the implementation use the configurable horn_mute_min_altitude_ft
  (default 200 ft). Amend REQ-012 to name horn_mute_min_altitude_ft with 200 ft as its default, and
  list it under parameters. Do not change the behaviour." --apply
request 1: gpt-6.1-sol, 6718 tokens in, 489 out
# Amend REQ-012 to express its mute boundary using horn_mute_min_altitude_ft, retaining 200 ft as the
# default and preserving behavior.

$ sparrow ai tests REQ-010 --apply
request 1: gpt-6.1-sol, 19454 tokens in, 8778 out
# Correct tests that assumed an armed warning immediately after initialization, and extend REQ-010
# coverage for configured thresholds and same-step disarming.
```

That run added `fixture_arm_warning()` to the fixture and fixed all 11 tests, including the horn tests that cite other requirements. The next three runs (`tests REQ-011`, `REQ-012`, `REQ-013 --verify`) were given the same failures, recorded before the fix, and each declined to change a test:

```text
$ sparrow ai tests REQ-012 --apply
request 1: gpt-6.1-sol, 15969 tokens in, 4137 out
# Add REQ-012 boundary coverage during a latched gear-disagree fault and alternate extension, without
# weakening the existing failing tests.
#
# By inspection, the three reported failing tests already establish the REQ-014 arming prerequisite,
# and their expected warning alarms are correct. ... If reproduced, they require controller/build
# investigation rather than changed expectations.

$ sparrow ai tests REQ-013 --verify
request 1: gpt-6.1-sol, 16850 tokens in, 3482 out
gate pass: make gen
gate pass: make format
gate pass: make trace
gate pass: make lint
gate pass: make test
```

The stale results were Sparrow's mistake; results older than their test file are now left out. With the change in, the take-off is quiet and the forgotten-gear approach still sounds the horn.

### Review

```text
$ git add -N examples/landing-gear          # let git diff see the new files
$ sparrow ai review main
request 1: gpt-6.1-sol, 80787 tokens in, 1327 out
The controller logic appears consistent with REQ-001 through REQ-014, including timeout precedence,
interlocks, alternate extension, and warning arming. Review found a document-validation failure
path, an inconsistent alarm-acknowledgement contract, and verification gaps. Gates were not
executed; findings are based on the supplied diff.

ERROR   sparrow/codegen/model.py:167; sparrow/ai/session.py:68-71: Malformed model structure types
bypass the new proposal-validation error handling. For example, replacing `structs: { ... }` with
`structs: []` reaches `raw["structs"].items()` and raises AttributeError rather than ModelError.
check_documents catches only ModelError for model proposals, so this aborts the task instead of
producing a ProposalError and using the repair/retry flow. Validate that structs is a mapping
before calling items(), and add a malformed-container regression test.
WARNING examples/landing-gear/model/gear.yaml:88; examples/landing-gear/controller/gear_controller.c:190-213
[REQ-008, REQ-011]: The model defines mute as requesting both horn muting and alarm
acknowledgement, but the controller never acknowledges alarms on mute. Both fault-update functions
set alarms.unacked, and only an accepted gear-disagree reset clears its bit. ... Resolve the
contract explicitly: either specify and implement acknowledgement without clearing the fault
latch/master warning, or remove the unsupported acknowledgement promise from the model.
WARNING examples/landing-gear/tests/python/test_gear_plant.py:1-128 [REQ-004, REQ-005, REQ-007,
REQ-009, REQ-010, REQ-014]: The only added Python test module exercises GearPlant directly; none of
the tests uses GearBench or cites a requirement. Consequently, the new ctypes binding,
controller/plant feedback, and supplied scenarios have no automated closed-loop verification. Add
requirement-marked closed-loop tests for normal extension/retraction, failed hydraulic transit
followed by alternate recovery, and takeoff suppression followed by an armed approach warning.
WARNING examples/landing-gear/model/gear.yaml:109-124; examples/landing-gear/controller/gear_controller.c:5-30:
The new configuration acceptance limits are not tested at their boundaries. ...
```

All four were valid:

- The ERROR was in a Sparrow fix made earlier in this run. `outputs` and `structs` are now checked to be mappings.
- The acknowledgement promise came from the hand-written interface, not from a requirement. It was removed from the `mute` description.
- The missing closed-loop tests are the gap the tests stage had reported seven times. The tests task now also receives the project modules `conftest.py` imports (the bench and the plant) and the generated Python module, and two runs wrote closed-loop tests:

```text
$ sparrow ai tests REQ-009 --apply
request 1: gpt-6.1-sol, 23846 tokens in, 11117 out
# Add continuous-hold C coverage and closed-loop tests for hydraulic independence, free-fall
# reversal, fault behavior, and ground lever locking.
+++ b/examples/landing-gear/tests/python/test_alternate_priority_closed_loop.py

$ sparrow ai tests REQ-014 --verify
request 1: gpt-6.1-sol, 30781 tokens in, 7336 out
+++ b/examples/landing-gear/tests/python/test_warning_arming_flight_history.py
gate pass: make gen
gate FAIL: make format
E501 Line too long (101 > 100)
   --> sparrow/ai/tasks.py:146:101
```

  The failure was a line in Sparrow written by hand in the same fix. With it wrapped, all gates passed: 25 C suites, 196 pytest tests, and the C suites again under AddressSanitizer and UBSan (`make test-asan`). The closed-loop tests fly the plant: free-fall at hydraulic pressures just below, at and above the actuation limit, alternate extension taking over a moving gear, climbs that peak just below, at and above 500 ft followed by an approach, and a second take-off after a touchdown.
- Configuration limits at their boundaries: not done. Configuration validation is not among the requirements of this example.

### Final runs

```text
$ python -m landing_gear takeoff
  time    alt  kias    hyd  N L R  controller      event
  0.00      0   100   3000  G G G  Initializing    start
  0.05      0   100   3000  G G G  Down and locked controller: Down and locked
 13.35      2   140   3000  G G G  Down and locked liftoff
 17.00    123   151   3000  G G G  Down and locked pilot: gear lever UP
 17.00    123   151   3000  G G G  Down and locked pilot: accelerating 1 kt/s
 17.05    125   151   3000  R R R  Retraction selected controller: Retraction selected
 17.05    125   151   3000  R R R  Retraction selected gear lights R R R
 25.05    392   159   3000  - - -  Retraction selected gear lights - - -
 25.10    393   159   3000  - - -  Up and locked   controller: Up and locked

end at 35.0 s: Up and locked, lights - - -, alarms: none
```

```text
$ python -m landing_gear gear-not-down
  time    alt  kias    hyd  N L R  controller      event
  0.00   1000   150   3000  - - -  Initializing    start
  0.05    999   150   3000  - - -  Up and locked   controller: Up and locked
 33.40    499   150   3000  - - -  Up and locked   caution: Valid low altitude and low airspeed with gear not down and locked
 33.40    499   150   3000  - - -  Up and locked   gear horn ON
 36.00    460   150   3000  - - -  Up and locked   pilot: MUTE pressed
 36.05    459   150   3000  - - -  Up and locked   gear horn off
 53.40    199   150   3000  - - -  Up and locked   gear horn ON
 55.00    175   150   3000  - - -  Up and locked   pilot: MUTE pressed
 56.00    160   150   3000  - - -  Up and locked   pilot: gear lever DOWN
 56.05    159   150   3000  R R R  Extending       controller: Extending
 56.05    159   150   3000  R R R  Extending       gear lights R R R
 63.05     54   150   3000  G G G  Extending       gear lights G G G  three green
 63.10     54   150   3000  G G G  Down and locked controller: Down and locked
 63.10     54   150   3000  G G G  Down and locked clears: Valid low altitude and low airspeed with gear not down and locked
 63.10     54   150   3000  G G G  Down and locked gear horn off
 66.70      0   150   3000  G G G  Down and locked touchdown, gear down and locked

end at 75.0 s: Down and locked, lights G G G, alarms: none
```

The second MUTE, at 175 ft, is refused: below 200 ft the horn cannot be silenced (REQ-012).

```text
$ python -m landing_gear hydraulic-failure
  time    alt  kias    hyd  N L R  controller      event
  0.00   2000   170   3000  - - -  Initializing    start
  0.05   2000   170   3000  - - -  Up and locked   controller: Up and locked
  5.00   1958   170   3000  - - -  Up and locked   FAILURE: Hydraulic pump failed
 15.00   1875   170      0  - - -  Up and locked   pilot: gear lever DOWN
 15.05   1875   170      0  - - -  Extending       controller: Extending
 30.05   1750   170      0  - - -  Fault           controller: Fault
 30.05   1750   170      0  - - -  Fault           WARNING: Normal hydraulic transit did not complete before its timeout
 30.05   1750   170      0  - - -  Fault           master warning ON
 34.00   1717   170      0  - - -  Fault           pilot: MUTE pressed
 40.00   1667   170      0  - - -  Fault           pilot: alternate extension handle PULLED
 40.05   1666   170      0  R R R  Fault           gear lights R R R
 52.05   1566   170      0  G G G  Fault           gear lights G G G  three green
 56.00   1533   170      0  G G G  Fault           pilot: RESET pressed
 56.05   1533   170      0  G G G  Alternate extension controller: Alternate extension
 56.05   1533   170      0  G G G  Alternate extension clears: Normal hydraulic transit did not complete before its timeout
 56.05   1533   170      0  G G G  Alternate extension master warning off

end at 70.0 s: Alternate extension, lights G G G, alarms: none
```

With no pressure the uplocks hold the gear up, the transit times out after 15 s, and MUTE leaves the master warning lit (REQ-008). The handle releases the uplocks, the gear free-falls in 12 s, and the reset is accepted because the gear now agrees with the lever.

```text
$ python -m landing_gear squat-switch-failure
  time    alt  kias    hyd  N L R  controller      event
  0.00      0    20   3000  G G G  Initializing    start
  0.05      0    20   3000  G G G  Down and locked controller: Down and locked
  3.00      0    20   3000  G G G  Down and locked pilot: gear lever UP -- lever lock holds it down
  6.00      0    20   3000  G G G  Down and locked FAILURE: Weight-on-wheels switches stuck in air
  8.00      0    20   3000  G G G  Down and locked pilot: gear lever UP
  8.05      0    20   3000  G G G  Retraction selected controller: Retraction selected

end at 15.0 s: Retraction selected, lights G G G, alarms: none
```

With both squat switches reading "air" the lever lock releases, but 20 kt is below the 80 kt minimum, so the up valve is never energized.

```text
$ python -m landing_gear nose-gear-jammed
  time    alt  kias    hyd  N L R  controller      event
  0.00   2000   170   3000  - - -  Initializing    start
  0.05   2000   170   3000  - - -  Up and locked   controller: Up and locked
  2.00   1983   170   3000  - - -  Up and locked   FAILURE: Nose gear jammed
  5.00   1958   170   3000  - - -  Up and locked   pilot: gear lever DOWN
  5.05   1958   170   3000  - R R  Extending       controller: Extending
  5.05   1958   170   3000  - R R  Extending       gear lights - R R
 12.05   1900   170   3000  - G G  Extending       gear lights - G G
 20.05   1833   170   3000  - G G  Fault           controller: Fault
 20.05   1833   170   3000  - G G  Fault           WARNING: Normal hydraulic transit did not complete before its timeout
 20.05   1833   170   3000  - G G  Fault           master warning ON
 30.00   1750   170   3000  - G G  Fault           pilot: alternate extension handle PULLED

end at 50.0 s: Fault, lights - G G, alarms: Normal hydraulic transit did not complete before its timeout
```

`approach` is unchanged from the run shown under [Code](#4-code): three green at 789 ft and a normal touchdown.

## Human edits

To AI output, all small:

- Model: the requirement citations on `GEAR_DISAGREE` and `TOO_LOW_GEAR`; `normal_transit_timeout_ms` as `u32`.
- Model: the MUTE acknowledgement promise removed, after the review. It came from the hand-written interface.
- Requirements: the `examples/landing-gear/` prefix removed from the implementation references.
- Formatting, by the `make format` gate.

None to the controller and none to its tests.

## Sparrow fixes

Building a project from nothing exposed defects the Smart Boiler, which grew one feature at a time, did not. Each has a test in `tests/python`.

- `sparrow gen` generates valid C and Python for a model with no faults yet, and requires at least one state and one config parameter.
- The model loader names a missing key or a malformed section instead of raising `KeyError` or `AttributeError`.
- `sparrow ai` loads a proposed model or requirement file before applying it, and sends an invalid one back as a rejected reply.
- The code task says which directory implementation references are relative to.
- The tests task receives any `*_fixture.h`, the modules `conftest.py` imports and the generated Python module, and no longer names `boiler_step`.
- The tests task receives the failures of the tests that cite its requirement, unless the results are older than the test file, and is asked to correct them in place.
- The trace summary for `analyze` no longer adds the boiler's temperature to every state change.

## Limitations

- **No reason for the crew.** Retraction inhibited by the interlock (`squat-switch-failure`) leaves the gear down with no annunciation; none of the six behaviours asks for one.
- **State after free-fall.** With the handle still pulled after a reset, the state reads "Alternate extension" although the gear is down and locked.
- **Arming after a restart.** A controller restarted in flight below 500 ft does not arm the gear warning until the aircraft climbs above it (REQ-014 starts disarmed).
- **Not tested:** configuration limits at their boundaries (from the review), hydraulic pressure monitoring, overspeed, and proximity sensor faults, which were outside the six behaviours.
- **Not predictive.** The plant exists to exercise the controller with plausible timing and failures; it does not predict a real aircraft.
