# Copyright 2026 RRRTX Labs — themes core build + test orchestration (P8-T3/T4).
#
# Std-only: g++ (C++20) + make. Zero external dependencies (phase DEPENDENCY
# RULES). The themes CORE is std-only and self-contained (its own copied JSON
# model, namespace xr::themes).
#
# Flags PINNED here and recorded in evidence logs (CI parity):
#   CXXFLAGS = -std=c++20 -O2 -Wall -Wextra -Werror -D_FORTIFY_SOURCE=2
#
# Targets: build | test | bench | clean. A bare `make` builds the real
# targets (themes_host + suites + bench_main) — the xr-browser parity test
# invokes `make` with no target on a clean clone (CI).
#
# The fuzz suite is wall-clock budgeted (default 30 s unit budget; the P8
# 600 s seeded campaign runs as XR_FUZZ_SECONDS=600 and is recorded in
# evidence logs — never a silent pass).

CXX      ?= g++
CXXFLAGS := -std=c++20 -O2 -Wall -Wextra -Werror -D_FORTIFY_SOURCE=2
INCLUDES := -I../..
BUILD    ?= build
ROOT     := $(abspath ../..)

.DEFAULT_GOAL := build

CORE_OBJS := \
  $(BUILD)/themes/core/json.o \
  $(BUILD)/themes/core/json_parse.o \
  $(BUILD)/themes/core/sha256.o \
  $(BUILD)/themes/core/theme.o \
  $(BUILD)/themes/core/contrast.o \
  $(BUILD)/themes/core/loader.o

TESTS := test_theme test_contrast test_loader test_host test_fuzz

$(BUILD)/themes/core/%.o: ../../themes/core/%.cc
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/themes_host: ../../themes/host/themes_host.cc $(CORE_OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< $(CORE_OBJS) -o $@

$(BUILD)/bench_main: ../../themes/bench/bench_main.cc $(CORE_OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< $(CORE_OBJS) -o $@

$(BUILD)/test_%: test_%.cc $(CORE_OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< $(CORE_OBJS) -o $@

build: $(BUILD)/themes_host $(BUILD)/bench_main $(TESTS:%=$(BUILD)/%)

test: build
	@rc=0; for t in $(TESTS); do \
	  echo "== $$t =="; \
	  ./$(BUILD)/$$t || rc=1; \
	done; \
	if [ $$rc -eq 0 ]; then echo "ALL C++ THEMES TESTS PASSED"; else echo "C++ THEMES TESTS FAILED"; fi; \
	exit $$rc

bench: $(BUILD)/bench_main
	cd $(BUILD) && ./bench_main --tokens ../../ui/themes/tokens.json

clean:
	rm -rf $(BUILD)
