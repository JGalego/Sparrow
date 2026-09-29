# 4. A minimal C test harness instead of GoogleTest

Status: accepted

## Context

The controller and core library are C11. GoogleTest would add a C++ toolchain to every build, including cross builds, and a fetched dependency. Traceability also needs each test to declare the requirements it verifies, in a form a script can read without compiling.

## Decision

`sparrow/testing/sp_test.h` provides `SP_TEST(name, "REQ-...")` with self-registration and a small set of assertions. `sp_test_main.c` runs the tests and writes JUnit XML with the requirement IDs as properties. Each test executable is one CTest test.

## Consequences

- C tests build with the same compiler and flags as the code under test, including sanitizers.
- `sparrow trace` finds C and Python tests with the same kind of declaration and reads both result formats.
- There are no fixtures, parameterized tests or death tests. Fixtures are plain structs (`tests/c/boiler_fixture.h`), and parameterized cases are written out. So far that has been enough.
