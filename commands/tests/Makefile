# Copyright 2026 RRRTX Labs — command registry core build + test orchestration (P7).
#
# Std-only: g++ (C++20) + make. Zero external dependencies (phase
# DEPENDENCY RULES). The commands CORE is std-only and self-contained (its own
# copied JSON model, namespace xr::commands). Only the DIAL integration test
# links policy_core (to prove the host writes genuine P6 policy state).
#
# Flags PINNED here and recorded in evidence logs (CI parity):
#   CXXFLAGS = -std=c++20 -O2 -Wall -Wextra -Werror -D_FORTIFY_SOURCE=2
#
# Targets: build | test | bench | clean.
#
# This Makefile lives in commands/tests/. Object files mirror their source
# path under build/ so the copied JSON model in commands/core does not collide
# with policy/core's objects.

CXX      ?= g++
CXXFLAGS := -std=c++20 -O2 -Wall -Wextra -Werror -D_FORTIFY_SOURCE=2
INCLUDES := -I../..
BUILD    ?= build
ROOT     := $(abspath ../..)
ROOT_ROSTER := $(ROOT)/commands/core/roster_v1.json

XR_BROWSER_ROOT ?= ../../../xr-browser
ENV := XR_BROWSER_ROOT=$(XR_BROWSER_ROOT) XR_COMMANDS_HOST=./$(BUILD)/commands_host

CORE_OBJS := \
  $(BUILD)/commands/core/json.o \
  $(BUILD)/commands/core/json_parse.o \
  $(BUILD)/commands/core/sha256.o \
  $(BUILD)/commands/core/descriptor.o \
  $(BUILD)/commands/core/registry.o \
  $(BUILD)/commands/core/matcher.o \
  $(BUILD)/commands/core/availability.o \
  $(BUILD)/commands/core/dispatch.o \
  $(BUILD)/commands/core/shortcuts.o \
  $(BUILD)/commands/core/policy_state.o

HOST_OBJS := $(BUILD)/commands/host/protocol.o
MAIN_OBJ  := $(BUILD)/commands/host/main.o

# Minimal policy set for the dial proof: json model + EffectivePolicy + the
# pure resolver (TrustTierIndex) + the P6 store. (service/cache/snapshot/events
# would drag in the enterprise managed-source TU — not needed here.)
POLICY_OBJS := \
  $(BUILD)/policy/core/json.o \
  $(BUILD)/policy/core/json_parse.o \
  $(BUILD)/policy/core/sha256.o \
  $(BUILD)/policy/core/effective_policy.o \
  $(BUILD)/policy/core/resolve.o \
  $(BUILD)/policy/core/store.o

TESTS := test_descriptor test_registry test_matcher test_availability \
         test_dispatch test_shortcuts test_dial test_host test_fuzz

.PHONY: build test bench clean

$(BUILD)/commands/core/%.o: ../../commands/core/%.cc
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/commands/host/protocol.o: ../../commands/host/protocol.cc
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/commands/host/main.o: ../../commands/host/commands_host.cc
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -DXR_DEFAULT_ROSTER='"$(ROOT_ROSTER)"' -c $< -o $@

$(BUILD)/policy/core/%.o: ../../policy/core/%.cc
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/bench_main: ../../commands/bench/bench_main.cc $(CORE_OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< $(CORE_OBJS) -o $@

$(BUILD)/commands_host: $(HOST_OBJS) $(MAIN_OBJ) $(CORE_OBJS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $^ -o $@

$(BUILD)/test_%: test_%.cc $(CORE_OBJS) $(HOST_OBJS) $(POLICY_OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< $(CORE_OBJS) $(HOST_OBJS) $(POLICY_OBJS) -o $@ -pthread

build: $(BUILD)/commands_host $(BUILD)/bench_main $(TESTS:%=$(BUILD)/%)

test: build
	@rc=0; for t in $(TESTS); do \
	  echo "== $$t =="; \
	  $(ENV) ./$(BUILD)/$$t || rc=1; \
	done; \
	if [ $$rc -eq 0 ]; then echo "ALL C++ COMMANDS TESTS PASSED"; else echo "C++ COMMANDS TESTS FAILED"; fi; \
	exit $$rc

bench: $(BUILD)/bench_main
	@cd $(BUILD) && ../$(BUILD)/bench_main > bench-results.json 2>/dev/null || \
	( cd $(BUILD) && ../$(BUILD)/bench_main )

clean:
	rm -rf $(BUILD)
