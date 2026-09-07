"""Shared helpers for xr.mojom Python behavioral fakes (P5 freeze).

These are BEHAVIORAL fakes, not bindings. They let dependent tracks (P6-P10)
code against the frozen contracts from day one over a documented stdio JSON
protocol (see fakes/README.md). The real C++ mojom bindings are farm-gated
(HG-9/HG-27); these fakes never claim otherwise.

Contract laws honored here:
  * deterministic: no clock, no RNG, no environment, no I/O beyond loading
    declared fixtures/vectors. (PolicyResolver purity is contract-tested.)
  * deny-default / total: unknown input maps to a denying result, never an
    exception a caller could read as "allow".
  * typed errors: results are {"ok": ...} or {"error": "<ErrorCode>"}.

Stdlib only. Python 3.12+.
"""
from __future__ import annotations

import json
from typing import Any

CONTRACT_VERSION = 1

# ErrorCode enum names (mirror xr_types.mojom ErrorCode).
ERROR_CODES = {
    "kOk",
    "kUnknownIdentity",
    "kUnknownOrigin",
    "kUnknownRequestClass",
    "kMalformedInput",
    "kFailClosed",
    "kNotPermitted",
    "kVersionMismatch",
    "kUnavailable",
}


def ok(value: Any) -> dict[str, Any]:
    return {"ok": value}


def err(code: str) -> dict[str, Any]:
    if code not in ERROR_CODES:
        raise ValueError(f"unknown ErrorCode: {code}")
    return {"error": code}


def canonical(obj: Any) -> str:
    """Byte-stable canonical JSON: sorted keys, no spaces, no timestamps."""
    return json.dumps(obj, sort_keys=True, separators=(",", ":"))
