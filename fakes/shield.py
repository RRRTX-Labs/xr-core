"""Behavioral fake for xr.mojom.Shield (+ BlockEvent). Deterministic; stdlib."""
from __future__ import annotations

import json
import sys
from typing import Any

from _base import canonical, err, ok


def status(identity: dict, origin: dict) -> dict[str, Any]:
    if not identity.get("value"):
        return err("kUnknownIdentity")
    if origin.get("scheme") not in {"http", "https"}:
        return err("kUnknownOrigin")
    return ok({"enabled": True, "blocked_count": 0})


def recent_events(identity: dict, max_events: int) -> dict[str, Any]:
    # Deterministic fixed sample (no clock): fields per §1.5 event tuple.
    sample = {
        "ts_millis": 1000,
        "identity": identity,
        "tab_id": 1,
        "origin": {"scheme": "https", "registrable_domain": "tracker.example"},
        "target": "https://tracker.example/a.js",
        "rule": "||tracker.example^",
        "list_provenance": "xr-default-list-v1",
        "action": "kBlocked",
        "request_class": "kScript",
    }
    n = max(0, min(int(max_events), 1))
    return ok([sample] * n)


def call(method: str, args: dict[str, Any]) -> dict[str, Any]:
    if method == "Status":
        return status(args.get("identity", {}), args.get("origin", {}))
    if method == "RecentEvents":
        return recent_events(args.get("identity", {}), args.get("max_events", 0))
    return err("kMalformedInput")


def main(argv: list[str]) -> int:
    payload = json.loads(argv[1] if len(argv) > 1 else sys.stdin.read())
    print(canonical(call(payload.get("method", ""), payload.get("args", {}))))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
