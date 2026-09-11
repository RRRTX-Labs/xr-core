# Copyright 2026 RRRTX Labs — policy core build + test orchestration (P6).
#
# Std-only: g++ (C++20) + make. Zero external dependencies (phase
# DEPENDENCY RULES). Flags are PINNED here and recorded in evidence logs:
#   CXXFLAGS = -std=c++20 -O2 -Wall -Wextra -Werror -D_FORTIFY_SOURCE=2
# No LTO, no fast-math, no profile tricks: budgets are measured honestly.
#
# Targets:
#   make build   — compile all core TUs, tests, host, bench
#   make test    — build + run every C++ test binary (exit != 0 on failure)
#   make bench   — build + run the microbenchmark (writes bench-results.json)
#   make clean
#
# XR_BROWSER_ROOT locates the golden vectors (cross-repo); default assumes
# sibling checkouts as in CI and the dev sandbox.

CXX      ?= g++
# -D_FORTIFY_SOURCE=2 mirrors the hardened ubuntu-24.04 CI runners (their
# gcc enables fortify implicitly at -O2): a discarded fopen/fwrite result is
# -Werror=unused-result on CI. Build the same way locally so this failure
# class cannot pass here and fail there (it did once: run 34167592053).
CXXFLAGS := -std=c++20 -O2 -Wall -Wextra -Werror -D_FORTIFY_SOURCE=2
INCLUDES := -I../..
BUILD    ?= build

XR_BROWSER_ROOT ?= ../../../xr-browser
VECTORS_ENV := XR_BROWSER_ROOT=$(XR_BROWSER_ROOT)

# P11-T0-b: json/json_parse/sha256 moved to the SINGLE shared copy in
# ../../common/core (ADR-0043); this suite compiles those objects and the
# shared KAT so the lane still pins the primitives it consumes.
COMMON_OBJS := $(BUILD)/common_json.o $(BUILD)/common_json_parse.o $(BUILD)/common_sha256.o

CORE_SRCS := \
  ../../policy/core/effective_policy.cc \
  ../../policy/core/resolve.cc \
  ../../policy/core/resolve_io.cc \
  ../../policy/core/cache.cc \
  ../../policy/core/snapshot.cc \
  ../../policy/core/store.cc \
  ../../policy/core/events.cc \
  ../../policy/core/service.cc \
  ../../policy/enterprise/managed_source.cc

CORE_OBJS := $(patsubst %.cc,$(BUILD)/%.o,$(notdir $(CORE_SRCS))) $(COMMON_OBJS)

TESTS := test_vectors test_resolve test_purity test_cache test_snapshot test_store test_events test_enterprise test_service test_sha256_kat

.PHONY: build test bench clean

$(BUILD)/%.o: ../../policy/core/%.cc
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/common_%.o: ../../common/core/%.cc
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# The ONE shared KAT source (common/tests), compiled into this lane too —
# coverage cannot desynchronize (P11-T0-b). test_json moved to the common
# lane with the parser it pins.
$(BUILD)/test_sha256_kat: ../../common/tests/test_sha256_kat.cc $(COMMON_OBJS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< $(COMMON_OBJS) -o $@

$(BUILD)/managed_source.o: ../../policy/enterprise/managed_source.cc
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/policy_host: ../../policy/host/policy_host.cc $(CORE_OBJS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< $(CORE_OBJS) -o $@

$(BUILD)/bench_main: ../../policy/bench/bench_main.cc $(CORE_OBJS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< $(CORE_OBJS) -o $@

$(BUILD)/test_%: test_%.cc $(CORE_OBJS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< $(CORE_OBJS) -o $@ -pthread

build: $(BUILD)/policy_host $(BUILD)/bench_main $(TESTS:%=$(BUILD)/%)

test: build
	@rc=0; for t in $(TESTS); do \
	  echo "== $$t =="; \
	  $(VECTORS_ENV) ./$(BUILD)/$$t || rc=1; \
	done; \
	if [ $$rc -eq 0 ]; then echo "ALL C++ POLICY TESTS PASSED"; else echo "C++ POLICY TESTS FAILED"; fi; \
	exit $$rc

bench: $(BUILD)/bench_main
	@cd $(BUILD) && ../$(BUILD)/bench_main > bench-results.json 2>/dev/null || \
	  ( cd $(BUILD) && ../$(BUILD)/bench_main )

clean:
	rm -rf $(BUILD)
