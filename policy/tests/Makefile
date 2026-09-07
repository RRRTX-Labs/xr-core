# Copyright 2026 RRRTX Labs — policy core build + test orchestration (P6).
#
# Std-only: g++ (C++20) + make. Zero external dependencies (phase
# DEPENDENCY RULES). Flags are PINNED here and recorded in evidence logs:
#   CXXFLAGS = -std=c++20 -O2 -Wall -Wextra -Werror
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
CXXFLAGS := -std=c++20 -O2 -Wall -Wextra -Werror
INCLUDES := -I../..
BUILD    ?= build

XR_BROWSER_ROOT ?= ../../../xr-browser
VECTORS_ENV := XR_BROWSER_ROOT=$(XR_BROWSER_ROOT)

CORE_SRCS := \
  ../../policy/core/json.cc \
  ../../policy/core/json_parse.cc \
  ../../policy/core/sha256.cc \
  ../../policy/core/effective_policy.cc \
  ../../policy/core/resolve.cc \
  ../../policy/core/resolve_io.cc \
  ../../policy/core/cache.cc \
  ../../policy/core/snapshot.cc \
  ../../policy/core/store.cc \
  ../../policy/core/events.cc \
  ../../policy/core/service.cc \
  ../../policy/enterprise/managed_source.cc

CORE_OBJS := $(patsubst %.cc,$(BUILD)/%.o,$(notdir $(CORE_SRCS)))

TESTS := test_vectors test_resolve test_purity test_cache test_snapshot test_store test_events test_enterprise

.PHONY: build test bench clean

$(BUILD)/%.o: ../../policy/core/%.cc
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

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
