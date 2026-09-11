# Copyright 2026 RRRTX Labs — shield core build + test orchestration (P11-T2).
#
# Std-only: g++ (C++20) + make. Zero external dependencies (phase DEPENDENCY
# RULES). The shield CORE is std-only and self-contained; json/sha256 come
# from the SINGLE shared copy in ../../common/core (P11-T0-b, ADR-0043) and
# the shared KAT is compiled into this lane so coverage cannot desync.
#
# Flags PINNED here and recorded in evidence logs (CI parity):
#   CXXFLAGS = -std=c++20 -O2 -Wall -Wextra -Werror -D_FORTIFY_SOURCE=2
#
# Targets: build | test | clean. The vectors suite reads the golden vectors
# from the xr-browser checkout (XR_BROWSER_ROOT, default ../../../xr-browser).
# The host suite drives XR_SHIELD_HOST (default ./build/shield_host) over
# stdin — never shell-quoted argv (the update/tests lesson).

CXX      ?= g++
CXXFLAGS := -std=c++20 -O2 -Wall -Wextra -Werror -D_FORTIFY_SOURCE=2
INCLUDES := -I../.. -I.
BUILD    ?= build

XR_BROWSER_ROOT ?= ../../../xr-browser
ENV := XR_BROWSER_ROOT=$(XR_BROWSER_ROOT) XR_SHIELD_HOST=./$(BUILD)/shield_host

.DEFAULT_GOAL := build

COMMON_OBJS := \
  $(BUILD)/common/core/json.o \
  $(BUILD)/common/core/json_parse.o \
  $(BUILD)/common/core/sha256.o

CORE_OBJS := \
  $(BUILD)/shield/core/context.o \
  $(BUILD)/shield/core/posture.o \
  $(BUILD)/shield/core/bundle.o \
  $(BUILD)/shield/core/scope.o \
  $(BUILD)/shield/core/apply.o \
  $(BUILD)/shield/core/events.o \
  $(BUILD)/shield/core/match.o \
  $(BUILD)/shield/core/fake_engine.o

CORE_OBJS += $(COMMON_OBJS)

TESTS := test_context test_bundle_strict test_posture test_scope \
         test_apply test_events test_match test_shield_host \
         test_golden_vectors test_sha256_kat

.PHONY: build test clean

$(BUILD)/shield/core/%.o: ../../shield/core/%.cc
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

# The TEST-ONLY host binds the TableEngine fake (shield/core/fake_engine.h);
# the vendored adblock-rust binding is the engine/ FFI shim (hosted lane).
$(BUILD)/shield_host: ../../shield/host/shield_host.cc $(CORE_OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< $(CORE_OBJS) -o $@

$(BUILD)/test_%: test_%.cc $(CORE_OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< $(CORE_OBJS) -o $@

build: $(BUILD)/shield_host $(TESTS:%=$(BUILD)/%)

test: build
	@rc=0; for t in $(TESTS); do \
	  echo "== $$t =="; \
	  $(ENV) ./$(BUILD)/$$t || rc=1; \
	done; \
	if [ $$rc -eq 0 ]; then echo "ALL C++ SHIELD TESTS PASSED"; else echo "C++ SHIELD TESTS FAILED"; fi; \
	exit $$rc

clean:
	rm -rf $(BUILD)
