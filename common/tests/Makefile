# Copyright 2026 RRRTX Labs — common core build + test orchestration (P11-T0-b).
#
# The shared-primitives lane: SHA-256 KAT + the strict-JSON edge corpus.
# Std-only: g++ (C++20) + make, zero external dependencies.
#
# Flags PINNED here and recorded in evidence logs (CI parity):
#   CXXFLAGS = -std=c++20 -O2 -Wall -Wextra -Werror -D_FORTIFY_SOURCE=2
#
# test_sha256_kat.cc is the ONE shared KAT source: every consumer suite
# (policy, commands, settings, themes, update, shield) compiles THIS SAME
# FILE into its own lane (see each suite's Makefile) so KAT coverage cannot
# desynchronize. This Makefile runs it for the common lane itself.
#
# A bare `make` must build the real targets (.DEFAULT_GOAL explicit — the
# P7/P10 clean-clone lesson). The P9 lane discovery runs this Makefile as
# its own named lane; ci_lane_discovery expects the
# "ALL C++ COMMON TESTS PASSED" banner.

CXX      ?= g++
CXXFLAGS := -std=c++20 -O2 -Wall -Wextra -Werror -D_FORTIFY_SOURCE=2
INCLUDES := -I../.. -I.
BUILD    ?= build

.DEFAULT_GOAL := build

COMMON_OBJS := \
  $(BUILD)/common/core/json.o \
  $(BUILD)/common/core/json_parse.o \
  $(BUILD)/common/core/sha256.o

TESTS := test_sha256_kat test_json

.PHONY: build test clean

$(BUILD)/common/core/%.o: ../../common/core/%.cc
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# The shared KAT: built here AND by every consumer suite from this same
# source path (../../common/tests/test_sha256_kat.cc).
$(BUILD)/test_sha256_kat: ../../common/tests/test_sha256_kat.cc $(COMMON_OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< $(COMMON_OBJS) -o $@

$(BUILD)/test_%: test_%.cc $(COMMON_OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< $(COMMON_OBJS) -o $@

build: $(TESTS:%=$(BUILD)/%)

test: build
	@rc=0; for t in $(TESTS); do \
	  echo "== $$t =="; \
	  ./$(BUILD)/$$t || rc=1; \
	done; \
	if [ $$rc -eq 0 ]; then echo "ALL C++ COMMON TESTS PASSED"; else echo "C++ COMMON TESTS FAILED"; fi; \
	exit $$rc

clean:
	rm -rf $(BUILD)
