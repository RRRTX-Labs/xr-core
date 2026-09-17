# Copyright 2026 RRRTX Labs — cosmetic core build + test orchestration (P12-T1).
#
# Std-only: g++ (C++20) + make. Zero external dependencies (phase DEPENDENCY
# RULES). The cosmetic CORE is std-only and self-contained; sha256 comes from
# the SINGLE shared copy in common/core (P11-T0-b, ADR-0043) — no second
# hash, and tools/no_new_crypto_check.py enforces that on the xr-browser side.
#
# Flags PINNED here and recorded in evidence logs (CI parity):
#   CXXFLAGS = -std=c++20 -O2 -Wall -Wextra -Werror -D_FORTIFY_SOURCE=2
#
# Targets: build | test | clean.
#
# This lane does NOT link the vendored adblock-rust engine. The core is the
# parser/validator/decider; the engine binding lives in ../engine (a Rust FFI
# shim, not a GN target here) and the host in ../host. Keeping them separate is
# what lets this lane run in a sandbox with no cargo/rustc — and the absence of
# cargo is a measured fact of this environment, so no test here may claim an
# engine result it did not produce.

CXX      ?= g++
CXXFLAGS := -std=c++20 -O2 -Wall -Wextra -Werror -D_FORTIFY_SOURCE=2
INCLUDES := -I../../.. -I.
BUILD    ?= build

XR_BROWSER_ROOT ?= ../../../../xr-browser
ENV := XR_BROWSER_ROOT=$(XR_BROWSER_ROOT)

.DEFAULT_GOAL := build

COMMON_OBJS := \
  $(BUILD)/common/core/json.o \
  $(BUILD)/common/core/json_parse.o \
  $(BUILD)/common/core/sha256.o

CORE_OBJS := \
  $(BUILD)/renderer/cosmetic/core/selector.o \
  $(BUILD)/renderer/cosmetic/core/pseudo.o \
  $(BUILD)/renderer/cosmetic/core/scope_key.o \
  $(BUILD)/renderer/cosmetic/core/style.o \
  $(BUILD)/renderer/cosmetic/core/keyset.o \
  $(BUILD)/renderer/cosmetic/core/degrade.o \
  $(BUILD)/renderer/cosmetic/core/blob.o

HOST_OBJS := $(BUILD)/renderer/cosmetic/host/cosmetic_host.o

CORE_OBJS += $(COMMON_OBJS)

TESTS := test_selector test_pseudo test_scope_key test_style \
         test_keyset test_degrade test_blob_strict

.PHONY: build test clean

$(BUILD)/renderer/cosmetic/core/%.o: ../../../renderer/cosmetic/core/%.cc
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/renderer/cosmetic/host/%.o: ../../../renderer/cosmetic/host/%.cc
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/common/core/%.o: ../../../common/core/%.cc
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# The ONE shared KAT source (common/tests), compiled into this lane too —
# coverage cannot desynchronize (P11-T0-b, ADR-0043).
$(BUILD)/test_sha256_kat: ../../../common/tests/test_sha256_kat.cc $(COMMON_OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< $(COMMON_OBJS) -o $@

# harness.h is a prerequisite, not just an include: without it listed, make
# has no rule that can produce the target's header dependency and reports "No
# rule to make target" instead of compiling.
# The cosmetic host: the C++ backend the golden vectors replay against, byte
# for byte, alongside fakes/cosmetic.py. It links the CORE only — no engine, no
# DOM, no browser. Depends on the .o rather than the .cc so make has a rule for
# every prerequisite.
$(BUILD)/cosmetic_host: $(HOST_OBJS) $(CORE_OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(HOST_OBJS) $(CORE_OBJS) -o $@

$(BUILD)/test_%: test_%.cc harness.h $(CORE_OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< $(CORE_OBJS) -o $@

build: $(TESTS:%=$(BUILD)/%) $(BUILD)/test_sha256_kat $(BUILD)/cosmetic_host

test: build
	@rc=0; for t in $(TESTS) test_sha256_kat; do \
	  echo "== $$t =="; \
	  $(ENV) ./$(BUILD)/$$t || rc=1; \
	done; \
	if [ $$rc -eq 0 ]; then echo "ALL C++ COSMETIC TESTS PASSED"; else echo "C++ COSMETIC TESTS FAILED"; fi; \
	exit $$rc

clean:
	rm -rf $(BUILD)
