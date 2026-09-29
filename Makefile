# Developer entry points. Everything here is a thin wrapper over cmake, pytest
# and the `sparrow` command; run those directly if you prefer.

VENV := $(CURDIR)/.venv
export PATH := $(VENV)/bin:$(PATH)
PRESET ?= host
BUILD_DIR := build/$(PRESET)
SOURCES_C := $(shell git ls-files '*.c' '*.h' | grep -v '/generated/')
PROJECT := examples/smart-boiler/sparrow.yaml

.PHONY: setup build test test-asan run run-sil lint format trace gen screenshots clean

setup:
	python3 -m venv $(VENV)
	$(VENV)/bin/pip install -e '.[dev,ai]'

build:
	cmake --preset $(PRESET)
	cmake --build --preset $(PRESET) --parallel

test: build
	ctest --preset $(PRESET)
	SPARROW_BUILD_DIR=$(BUILD_DIR) pytest --junit-xml=$(BUILD_DIR)/results/python.xml
	sparrow trace $(PROJECT) --results $(BUILD_DIR)/results/*.xml \
		--write $(BUILD_DIR)/traceability.md
	@echo "traceability with results: $(BUILD_DIR)/traceability.md"

# C tests under AddressSanitizer and UBSan. Python cannot load the
# instrumented controller library, so the pytest suites are not run here.
# ASLR is disabled for the test processes: GCC 12/13 sanitizer runtimes crash
# at random on kernels with high mmap randomization (vm.mmap_rnd_bits = 32).
test-asan:
	cmake --preset host-asan
	cmake --build --preset host-asan --parallel
	setarch -R ctest --preset host-asan

run: build
	scripts/run-desktop.sh

run-sil: build
	scripts/run-sil.sh

lint:
	ruff check .
	ruff format --check .
	clang-format --dry-run --Werror $(SOURCES_C)
	cppcheck --quiet --error-exitcode=1 --enable=warning,portability \
		-D__ORDER_LITTLE_ENDIAN__=1234 -D__BYTE_ORDER__=1234 \
		--suppress=missingIncludeSystem --inline-suppr -I sparrow/core \
		-I examples/smart-boiler/generated -I examples/smart-boiler/controller \
		sparrow/core examples/smart-boiler/controller examples/smart-boiler/generated
	sparrow gen --check examples/smart-boiler/model/boiler.yaml
	sparrow trace $(PROJECT) --check docs/traceability.md

format:
	ruff check --fix .
	ruff format .
	clang-format -i $(SOURCES_C)

gen:
	sparrow gen examples/smart-boiler/model/boiler.yaml

trace:
	sparrow trace $(PROJECT) --write docs/traceability.md

screenshots: build
	scripts/capture-screenshots.py

clean:
	rm -rf build
