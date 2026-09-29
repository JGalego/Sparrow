# Contributing

## Setup

```sh
make setup      # .venv with the sparrow tools, test and lint dependencies, AI SDKs
make build
make test
make lint
```

Linux with CMake 3.20+, GCC or Clang, a C++ compiler (LVGL's build requires one), Python 3.10+, SDL2 headers, cppcheck and Git.

## Making a change

A behaviour change starts with its requirement:

1. Add or change the requirement in `requirements/*.yaml`.
2. If it needs new interface fields, faults or parameters, change `model/*.yaml` and run `make gen`. Never edit generated files.
3. Implement it and add the `path::symbol` to the requirement's `implementation`.
4. Test each limit below, at and above, and the failure behaviour. Cite the requirement in every test.
5. `make trace` regenerates `docs/traceability.md`.
6. For HMI or plant changes, run `scripts/capture-screenshots.py` and look at the result.

`make lint` and `make test` must pass. CI runs both, the sanitizer build and the aarch64 cross build.

Changes drafted with `sparrow ai` ([docs/ai.md](docs/ai.md)) follow the same rules and the same review.

## Style

- C11, formatted by `make format` (clang-format, 100 columns). Small functions, explicit sizes, no allocation or clock access in controller code, no global mutable state outside `main()`.
- Python: ruff, formatted by `make format`.
- Documentation describes what exists. Keep it short and specific. Diagrams are Mermaid, next to the text they belong to. Screenshots come from `scripts/capture-screenshots.py` and are used without captions.
- Record decisions that constrain later work as a short ADR in `docs/adr/`.

## Licensing

Contributions are accepted under the MIT license. Third-party code needs a compatible license, noted where it is used.
