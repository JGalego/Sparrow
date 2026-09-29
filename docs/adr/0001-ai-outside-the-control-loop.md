# 1. AI stays outside the build, tests and control loop

Status: accepted

## Context

Sparrow uses language models to draft requirements, code and tests. A model's output varies between runs and providers, needs a network or a large local model, and cannot be certified. The controller must behave identically on a workstation, in CI and on the target.

## Decision

AI is an optional tool in `sparrow/ai`. Nothing else imports it, and its SDKs are an install extra. It produces patches, never direct writes. A patch is limited to the files its task may change (`ai.paths`), may not touch generated files, and is applied only on request. `--verify` then runs the project's gates. The deployed controller, the simulator, the build and every test run without it.

## Consequences

- A clone builds, tests and runs with no key and no network.
- A proposal that fails validation is rejected whole, so a stale or ambiguous edit is never applied in the wrong place.
- Every AI change is reviewed as an ordinary Git diff.
- Tasks cannot make open-ended edits across the repository. A change that spans stages is made one stage at a time.
