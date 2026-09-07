"""Behavioral fake for xr.mojom.GuardLedger. Single Dispatch chokepoint.
Deterministic; stdlib."""
from __future__ import annotations

import json
import sys
from typing import Any

from _base import canonical, err, ok

# Deterministic verdict policy: high-risk APIs prompt, unknown => deny.
_PROMPT_APIS = {"tabs.executeScript", "webRequest.onBeforeRequest"}
_ALLOW_APIS = {"storage.local.get", "runtime.sendMessage"}


def dispatch(req: dict) -> dict:
    api = req.get("api_call", "")
    if not req.get("identity", {}).get("value"):
        return err("kUnknownIdentity")
    if api in _PROMPT_APIS:
        return ok("kPrompt")
    if api in _ALLOW_APIS:
        return ok("kAllow")
    return ok("kDeny")  # deny-default.


def call(method: str, args: dict[str, Any]) -> dict:
    if method == "Dispatch":
        return dispatch(args.get("request", args))
    if method == "RecentUpdateDiffs":
        return ok([{"extension_id": "ext-1", "from_version": "1.0", "to_version": "1.1", "ts_millis": 1000}][: max(0, min(args.get("max_rows", 0), 1))])
    if method == "RecentEgress":
        return ok([{"extension_id": "ext-1", "destination": "api.example", "bytes": 128, "ts_millis": 1000}][: max(0, min(args.get("max_rows", 0), 1))])
    return err("kMalformedInput")


def main(argv: list[str]) -> int:
    payload = json.loads(argv[1] if len(argv) > 1 else sys.stdin.read())
    print(canonical(call(payload.get("method", ""), payload.get("args", {}))))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
