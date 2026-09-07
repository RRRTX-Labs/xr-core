"""Behavioral fake for xr.mojom.IdentityManager (P5 freeze).

Demonstrates the P4 provisioning-timing finding (ADR-0042): partition is fixed
at creation; attach-after-navigation is rejected. Bind failure is fail-closed.
Deterministic; stdlib only. See fakes/README.md for the stdio JSON protocol.
"""
from __future__ import annotations

import json
import sys
from typing import Any

from _base import canonical, err, ok

# Deterministic id sequence (no RNG): the fake mints ids from a fixed table so
# fixtures/vectors stay byte-stable.
_MINTS = [
    "xr:00000000-0000-4000-8000-000000000001",
    "xr:00000000-0000-4000-8000-000000000002",
    "xr:00000000-0000-4000-8000-0000000000ef",
]


class IdentityManagerFake:
    def __init__(self) -> None:
        self._n = 0
        self._state: dict[str, dict[str, Any]] = {}

    def create(self, grade: str = "kStandard", ephemeral: bool = False) -> dict:
        if grade not in {"kStandard", "kFortress"}:
            return err("kMalformedInput")
        if self._n >= len(_MINTS):
            return err("kFailClosed")  # fail-closed when the pool is exhausted.
        vid = _MINTS[self._n]
        self._n += 1
        info = {
            "id": {"value": vid},
            "state": "kActive",
            "grade": grade,
            "in_memory": bool(ephemeral),
        }
        self._state[vid] = info
        return ok(info)

    def _transition(self, vid: str, state: str) -> dict:
        info = self._state.get(vid)
        if info is None:
            return err("kUnknownIdentity")
        info = dict(info, state=state)
        self._state[vid] = info
        return ok(info)

    def activate(self, vid: str) -> dict:
        return self._transition(vid, "kActive")

    def hibernate(self, vid: str) -> dict:
        return self._transition(vid, "kHibernated")

    def promote(self, vid: str) -> dict:
        info = self._state.get(vid)
        if info is None:
            return err("kUnknownIdentity")
        info = dict(info, grade="kFortress")
        self._state[vid] = info
        return ok(info)

    def destroy(self, vid: str) -> dict:
        if vid not in self._state:
            return err("kUnknownIdentity")
        del self._state[vid]
        # zero-residual verification hook (§1.4); T0 ruling (2): skeleton
        # tolerated, zero persistent DATA writes.
        return ok({"zero_residual_verified": True})

    def move_tab(self, frm: str, to: str, tab_id: int, after_nav: bool = False) -> dict:
        # P4 timing finding: attaching to an already-navigated tab requires a
        # destructive reload; the contract rejects a non-destructive attach.
        if after_nav:
            return err("kNotPermitted")
        if to not in self._state:
            return err("kUnknownIdentity")
        return ok(self._state[to])


_DISPATCH = {
    "Create": lambda f, a: f.create(a.get("grade", "kStandard"), a.get("ephemeral", False)),
    "Activate": lambda f, a: f.activate(a.get("id", {}).get("value", "")),
    "Hibernate": lambda f, a: f.hibernate(a.get("id", {}).get("value", "")),
    "PromoteToFortressProfile": lambda f, a: f.promote(a.get("id", {}).get("value", "")),
    "Destroy": lambda f, a: f.destroy(a.get("id", {}).get("value", "")),
    "MoveTab": lambda f, a: f.move_tab(
        a.get("from", ""), a.get("to", ""), a.get("tab_id", 0), a.get("after_nav", False)
    ),
}


def call(method: str, args: dict[str, Any]) -> dict[str, Any]:
    fake = IdentityManagerFake()
    # seed prior identities so single-shot calls resolve deterministically
    for pre in args.get("_seed", []):
        fake.create(pre.get("grade", "kStandard"), pre.get("ephemeral", False))
    fn = _DISPATCH.get(method)
    if fn is None:
        return err("kMalformedInput")
    return fn(fake, args)


def main(argv: list[str]) -> int:
    payload = json.loads(argv[1] if len(argv) > 1 else sys.stdin.read())
    print(canonical(call(payload.get("method", ""), payload.get("args", {}))))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
