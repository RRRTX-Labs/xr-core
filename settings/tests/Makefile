# Copyright 2026 RRRTX Labs — settings core build + test orchestration (P8-T1).
#
# Std-only: g++ (C++20) + make. Zero external dependencies (phase DEPENDENCY
# RULES). The settings CORE is std-only and self-contained (its own copied
# JSON model, namespace xr::settings — the P6/P7 per-module pattern).
#
# Flags PINNED here and recorded in evidence logs (CI parity):
#   CXXFLAGS = -std=c++20 -O2 -Wall -Wextra -Werror -D_FORTIFY_SOURCE=2
#
# Targets: build | test | bench | clean. A bare `make` must build the real
# targets (settings_host + tests + bench) — the .DEFAULT_GOAL is explicit so
# a clean clone works (P7's CI lesson). The C++-vs-Python parity test in
# xr-browser (tools/tests/test_p8_settings_themes.py) invokes `make` with no
# target and expects $(BUILD)/settings_host.
#
# The fuzz suite is wall-clock budgeted (default 30 s unit budget; the P8
# 600 s seeded campaign runs as XR_FUZZ_SECONDS=600 and is recorded in
# evidence logs). Never weaken a budget: the 600 s run happens for real in
# Plan P8-T4 "600 s seeded" and the T1 test plan). `make test` therefore
# takes ~10 min on top of the other suites; that is the point (a shorter
# lane would weaken the timebox). Set XR_FAST_FUZZ=1 for a quick smoke
# during development ONLY — never in CI (run_checks does not set it).

CXX      ?= g++
CXXFLAGS := -std=c++20 -O2 -Wall -Wextra -Werror -D_FORTIFY_SOURCE=2
INCLUDES := -I../..
BUILD    ?= build
ROOT     := $(abspath ../..)
ROOT_SCHEMA := $(ROOT)/settings/core/settings_schema_v1.json

XR_BROWSER_ROOT ?= ../../../xr-browser
ENV := XR_BROWSER_ROOT=$(XR_BROWSER_ROOT) XR_SETTINGS_HOST=./$(BUILD)/settings_host

.DEFAULT_GOAL := build

CORE_OBJS := \
  $(BUILD)/settings/core/json.o \
  $(BUILD)/settings/core/json_parse.o \
  $(BUILD)/settings/core/sha256.o \
  $(BUILD)/settings/core/settings_schema.o \
  $(BUILD)/settings/core/sections.o \
  $(BUILD)/settings/core/search.o \
  $(BUILD)/settings/core/router.o \
  $(BUILD)/settings/core/counters.o

TESTS := test_settings_schema test_sections test_settings_search \
         test_recall_corpus test_router test_settings_host test_counters \
         test_settings_fuzz

.PHONY: build test bench clean

$(BUILD)/settings/core/%.o: ../../settings/core/%.cc
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/settings_host: ../../settings/host/settings_host.cc $(CORE_OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -DXR_DEFAULT_SETTINGS_SCHEMA='"$(ROOT_SCHEMA)"' $< $(CORE_OBJS) -o $@

$(BUILD)/bench_main: ../../settings/bench/bench_main.cc $(CORE_OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< $(CORE_OBJS) -o $@ -pthread

$(BUILD)/test_%: test_%.cc $(CORE_OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< $(CORE_OBJS) -o $@

build: $(BUILD)/settings_host $(BUILD)/bench_main $(TESTS:%=$(BUILD)/%)

test: build
	@rc=0; for t in $(TESTS); do \
	  echo "== $$t =="; \
	  $(ENV) ./$(BUILD)/$$t || rc=1; \
	done; \
	if [ $$rc -eq 0 ]; then echo "ALL C++ SETTINGS TESTS PASSED"; else echo "C++ SETTINGS TESTS FAILED"; fi; \
	exit $$rc

bench: $(BUILD)/bench_main
	@mkdir -p $(BUILD)
	cd $(BUILD) && ./bench_main > bench-results.json 2>/dev/null || \
	( cd $(BUILD) && ./bench_main )

clean:
	rm -rf $(BUILD)
