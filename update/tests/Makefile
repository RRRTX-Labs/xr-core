# Copyright 2026 RRRTX Labs — update core build + test orchestration (P10-T1).
#
# Std-only: g++ (C++20) + make. Zero external dependencies (phase DEPENDENCY
# RULES). The update CORE is std-only and self-contained (its own copied JSON
# model, namespace xr::update — the P6-P8 per-module pattern; the sha256 is
# the same in-tree FIPS implementation the other cores ship).
#
# Flags PINNED here and recorded in evidence logs (CI parity):
#   CXXFLAGS = -std=c++20 -O2 -Wall -Wextra -Werror -D_FORTIFY_SOURCE=2
#
# Targets: build | test | clean. A bare `make` must build the real targets
# (update_host + tests) — the .DEFAULT_GOAL is explicit so a clean clone
# works (the P7 CI lesson, twice red). The P9 lane discovery runs this
# Makefile as its own named lane; ci_lane_discovery expects the
# "ALL C++ UPDATE TESTS PASSED" banner.
#
# The fuzz suite is wall-clock budgeted (default 30 s unit budget; the P10
# >=600 s seeded never-accept campaign runs as XR_FUZZ_SECONDS=600 and is
# recorded in evidence logs — never a silently shorter run). Set
# XR_FAST_FUZZ=1 for a dev smoke ONLY; run_checks never sets it.
#
# The vectors suite reads the golden vectors from the xr-browser checkout
# (XR_BROWSER_ROOT, default ../../../xr-browser).

CXX      ?= g++
CXXFLAGS := -std=c++20 -O2 -Wall -Wextra -Werror -D_FORTIFY_SOURCE=2
INCLUDES := -I../.. -I.
BUILD    ?= build

XR_BROWSER_ROOT ?= ../../../xr-browser
ENV := XR_BROWSER_ROOT=$(XR_BROWSER_ROOT) XR_UPDATE_HOST=./$(BUILD)/update_host

.DEFAULT_GOAL := build

# P11-T0-b: json/json_parse/sha256 are the SINGLE shared copy in
# ../../common/core (ADR-0043); this lane compiles those objects and
# the shared KAT so it still pins the primitives it consumes.
COMMON_OBJS := \
  $(BUILD)/common/core/json.o \
  $(BUILD)/common/core/json_parse.o \
  $(BUILD)/common/core/sha256.o

CORE_OBJS := \
  $(BUILD)/update/core/manifest.o \
  $(BUILD)/update/core/verify_policy.o \
  $(BUILD)/update/core/epoch.o \
  $(BUILD)/update/core/cohort.o \
  $(BUILD)/update/core/backoff.o \
  $(BUILD)/update/core/seen.o

CORE_OBJS += $(COMMON_OBJS)

TESTS := test_manifest_strict test_verify_policy test_monotonic_downgrade \
         test_replay test_cohort test_backoff test_update_host \
         test_golden_vectors test_update_fuzz test_sha256_kat

# XR_UPDATE_HOST is a TEST-ONLY façade (see update/host/update_host.cc): it
# binds the stub verifier and refuses release channels in core policy. The
# production signing/verify binding is farm glue (update/BUILD.gn comment).
.PHONY: build test clean

$(BUILD)/update/core/%.o: ../../update/core/%.cc
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/common/core/%.o: ../../common/core/%.cc
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# The ONE shared KAT source (common/tests), compiled into this lane too —
# coverage cannot desynchronize (P11-T0-b, ADR-0043).
$(BUILD)/test_sha256_kat: ../../common/tests/test_sha256_kat.cc $(COMMON_OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< $(COMMON_OBJS) -o $@

$(BUILD)/update_host: ../../update/host/update_host.cc $(CORE_OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< $(CORE_OBJS) -o $@

$(BUILD)/test_%: test_%.cc $(CORE_OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< $(CORE_OBJS) -o $@

build: $(BUILD)/update_host $(TESTS:%=$(BUILD)/%)

test: build
	@rc=0; for t in $(TESTS); do \
	  echo "== $$t =="; \
	  $(ENV) ./$(BUILD)/$$t || rc=1; \
	done; \
	if [ $$rc -eq 0 ]; then echo "ALL C++ UPDATE TESTS PASSED"; else echo "C++ UPDATE TESTS FAILED"; fi; \
	exit $$rc

clean:
	rm -rf $(BUILD)
