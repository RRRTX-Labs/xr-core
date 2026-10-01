# Copyright 2026 RRRTX Labs — identity core build + test orchestration (P14-T1).
#
# Std-only: g++ (C++20) + make. Zero external dependencies (phase DEPENDENCY
# RULES). Flags PINNED here and recorded in evidence logs (CI parity):
#   CXXFLAGS = -std=c++20 -O2 -Wall -Wextra -Werror -D_FORTIFY_SOURCE=2
#
# Targets: build | test | clean. A bare `make` builds the real targets
# (identity_host + suites). The xr-browser suite-discovery lane runs
# `make test` here (depth-general discovery — a nested core cannot hide).
#
# The fuzz suite is wall-clock budgeted like every fleet target (the fleet
# law: XR_FUZZ_SECONDS drives the evidence campaign; the gate timebox is
# the fleet's own).

CXX      ?= g++
CXXFLAGS := -std=c++20 -O2 -Wall -Wextra -Werror -D_FORTIFY_SOURCE=2
INCLUDES := -I.. -I../.. -I.
BUILD    ?= build
ROOT     := $(abspath ../..)

.DEFAULT_GOAL := build

# P11-T0-b: json/json_parse/sha256 are the SINGLE shared copy in
# ../../common/core (ADR-0043); this lane compiles those objects and the
# shared KAT so it still pins the primitives it consumes.
COMMON_OBJS := \
  $(BUILD)/common/core/json.o \
  $(BUILD)/common/core/json_parse.o \
  $(BUILD)/common/core/sha256.o

CORE_OBJS := \
  $(BUILD)/identity/core/mint.o \
  $(BUILD)/identity/core/identity.o \
  $(BUILD)/identity/core/hibernate.o \
  $(BUILD)/identity/core/templates.o \
  $(BUILD)/identity/core/binding.o

CORE_OBJS += $(COMMON_OBJS)

TESTS := test_mint test_identity test_hibernate test_templates \
         test_binding test_sha256_kat

$(BUILD)/identity/core/%.o: ../core/%.cc
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


$(BUILD)/test_%: test_%.cc $(CORE_OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< $(CORE_OBJS) -o $@

build: $(TESTS:%=$(BUILD)/%)

test: build
	@rc=0; for t in $(TESTS); do \
	  echo "== $$t =="; \
	  ./$(BUILD)/$$t || rc=1; \
	done; \
	if [ $$rc -eq 0 ]; then echo "ALL C++ IDENTITY TESTS PASSED"; else echo "C++ IDENTITY TESTS FAILED"; fi; \
	exit $$rc

clean:
	rm -rf $(BUILD)
