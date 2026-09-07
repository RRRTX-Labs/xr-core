"""Behavioral fake for xr.mojom.RouteManager. Implements L6 fail-closed.

Loss => all pending/future requests error (kFailClosed) until recovery; recovery
requires a Bind carrying user_ack. Deterministic; stdlib. Golden vector:
xr-browser/docs/contracts/vectors/route-manager-v1.json.
"""
from __future__ import annotations

import json
import sys
from typing import Any

from _base import canonical, err, ok


class RouteManagerFake:
    def __init__(self) -> None:
        self._routes: dict[str, dict[str, Any]] = {}
        self._fail_closed = False

    def bind(self, vid: str, route: str, user_ack: bool = False) -> dict:
        if not vid:
            return err("kUnknownIdentity")
        if self._fail_closed and not user_ack:
            # recovery blocked until the user acknowledges the loss.
            return ok({"state": "kFailClosed", "route": route, "user_ack_required": True})
        self._fail_closed = False
        self._routes[vid] = {"state": "kBound", "route": route, "user_ack_required": False}
        return ok(self._routes[vid])

    def unbind(self, vid: str) -> dict:
        self._routes.pop(vid, None)
        return ok({"state": "kUnbound", "route": "kDirect", "user_ack_required": False})

    def status(self, vid: str) -> dict:
        if self._fail_closed:
            return ok({"state": "kFailClosed", "route": "kDirect", "user_ack_required": True})
        r = self._routes.get(vid)
        if r is None:
            return ok({"state": "kUnbound", "route": "kDirect", "user_ack_required": False})
        return ok(r)

    def lose_all(self) -> dict:
        self._fail_closed = True
        self._routes.clear()
        return ok({"state": "kFailClosed", "route": "kDirect", "user_ack_required": True})


def call(method: str, args: dict[str, Any], fake: RouteManagerFake | None = None) -> dict:
    fake = fake or RouteManagerFake()
    for step in args.get("_seq", []):
        call(step["method"], step.get("args", {}), fake)
    vid = args.get("identity", {}).get("value", "")
    if method == "Bind":
        return fake.bind(vid, args.get("route", "kDirect"), args.get("user_ack", False))
    if method == "Unbind":
        return fake.unbind(vid)
    if method == "Status":
        return fake.status(vid)
    if method == "LoseAllFailClosed":
        return fake.lose_all()
    return err("kMalformedInput")


def main(argv: list[str]) -> int:
    payload = json.loads(argv[1] if len(argv) > 1 else sys.stdin.read())
    print(canonical(call(payload.get("method", ""), payload.get("args", {}))))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
