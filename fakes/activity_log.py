"""Behavioral fake for xr.mojom.ActivityLog. Bounded-ring; deterministic."""
from __future__ import annotations

import json
import sys
from typing import Any

from _base import canonical, err, ok

_CAP = 3  # tiny ring for the fake/fixtures.


class ActivityLogFake:
    def __init__(self) -> None:
        self._ring: list[dict] = []

    def append(self, row: dict) -> dict:
        if row.get("kind") not in {
            "kPolicy", "kBlock", "kPermission", "kIdentity",
            "kRoute", "kVault", "kGuard", "kDownload",
        }:
            return err("kMalformedInput")
        self._ring.append(row)
        if len(self._ring) > _CAP:
            self._ring.pop(0)  # evict oldest (bounded ring).
        return ok("kOk")

    def query(self, identity, kind, max_rows) -> dict:
        rows = list(reversed(self._ring))  # newest first.
        if identity:
            rows = [r for r in rows if r.get("identity") == identity]
        if kind:
            rows = [r for r in rows if r.get("kind") == kind]
        return ok(rows[: max(0, int(max_rows))])


def call(method: str, args: dict[str, Any]) -> dict:
    fake = ActivityLogFake()
    for pre in args.get("_seed", []):
        fake.append(pre)
    if method == "Append":
        return fake.append(args.get("row", {}))
    if method == "Query":
        return fake.query(args.get("identity"), args.get("kind"), args.get("max_rows", 0))
    return err("kMalformedInput")


def main(argv: list[str]) -> int:
    payload = json.loads(argv[1] if len(argv) > 1 else sys.stdin.read())
    print(canonical(call(payload.get("method", ""), payload.get("args", {}))))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
