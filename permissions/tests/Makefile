# Copyright 2026 RRRTX Labs
# Use of this source code is governed by the MPL-2.0 license that can be
# found in the LICENSE file.
#
# permissions/tests/Makefile — P15 permission firewall C++ suite (ADR-0051).
#   make build  — compile the permissions core, policy core (for the cross-core
#                 conformance test), the suites, and the bench
#   make test   — build + run every suite; exits non-zero on any failure
#   make bench  — build + run the microbenchmark (writes bench-results.json)
#   make clean
# Std-only C++20, -Werror, same flags as policy/tests (reproducible off-tree).
# Lane discovery: tools/ci_lane_discovery.py finds this Makefile automatically.

CXX ?= g++
CXXFLAGS := -std=c++20 -O2 -Wall -Wextra -Werror -D_FORTIFY_SOURCE=2 -I../..
BUILD := build
XR_BROWSER_ROOT ?= ../../../xr-browser
export XR_BROWSER_ROOT

PERM_SRCS := types envelope store ops audit merge present
PERM_OBJS := $(PERM_SRCS:%=$(BUILD)/perm_%.o)
COMMON_OBJS := $(BUILD)/common_json.o $(BUILD)/common_json_parse.o
POLICY_OBJS := $(BUILD)/pol_effective_policy.o $(BUILD)/pol_resolve.o $(BUILD)/pol_resolve_io.o
LIB := $(PERM_OBJS) $(COMMON_OBJS)

TESTS := test_store test_ops test_expiry test_isolation test_merge test_present \
         test_envelope test_audit test_cross_core
BENCH := bench_main

.PHONY: build test bench clean

build: $(TESTS:%=$(BUILD)/%) $(BUILD)/$(BENCH)

$(BUILD)/.dir:
	mkdir -p $(BUILD) && touch $@

$(BUILD)/perm_%.o: ../core/%.cc ../core/*.h | $(BUILD)/.dir
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/common_json.o: ../../common/core/json.cc | $(BUILD)/.dir
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/common_json_parse.o: ../../common/core/json_parse.cc | $(BUILD)/.dir
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/pol_effective_policy.o: ../../policy/core/effective_policy.cc | $(BUILD)/.dir
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/pol_resolve.o: ../../policy/core/resolve.cc | $(BUILD)/.dir
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/pol_resolve_io.o: ../../policy/core/resolve_io.cc | $(BUILD)/.dir
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/test_cross_core: test_cross_core.cc $(LIB) $(POLICY_OBJS)
	$(CXX) $(CXXFLAGS) $< $(LIB) $(POLICY_OBJS) -o $@ -pthread

$(BUILD)/test_%: test_%.cc $(LIB)
	$(CXX) $(CXXFLAGS) $< $(LIB) -o $@ -pthread

$(BUILD)/$(BENCH): ../bench/$(BENCH).cc $(LIB) $(POLICY_OBJS)
	$(CXX) $(CXXFLAGS) $< $(LIB) $(POLICY_OBJS) -o $@ -pthread

test: build
	@set -e; for t in $(TESTS); do echo "== $$t =="; ./$(BUILD)/$$t; done
	@echo "ALL C++ PERMISSIONS TESTS PASSED"

bench: $(BUILD)/$(BENCH)
	@cd $(BUILD) && ./$(BENCH) > bench-results.json
	@cat $(BUILD)/bench-results.json

clean:
	rm -rf $(BUILD)
