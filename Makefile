# Developer entry points. Everything here is a thin wrapper over cmake, pytest
# and the `sparrow` command; run those directly if you prefer.

VENV := $(CURDIR)/.venv
export PATH := $(VENV)/bin:$(PATH)
PRESET ?= host
BUILD_DIR := build/$(PRESET)
SOURCES_C := $(shell git ls-files '*.c' '*.h' | grep -v '/generated/')
PROJECT := examples/smart-boiler/sparrow.yaml

.PHONY: setup build test run lint format trace gen screenshots clean

setup:
	python3 -m venv $(VENV)
	$(VENV)/bin/pip install -e '.[dev]'

build:
	cmake --preset $(PRESET)
	cmake --build --preset $(PRESET) --parallel

test: build
	ctest --preset $(PRESET)
	pytest --junit-xml=$(BUILD_DIR)/results/python.xml
	sparrow trace $(PROJECT) --check docs/traceability.md \
		--results $(BUILD_DIR)/results/*.xml

run: build
	scripts/run-desktop.sh

lint:
	ruff check .
	ruff format --check .
	clang-format --dry-run --Werror $(SOURCES_C)
	cppcheck --quiet --error-exitcode=1 --enable=warning,portability \
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
