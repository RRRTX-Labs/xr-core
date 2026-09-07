"""Behavioral fake for xr.mojom.VaultService. Absence-is-contract: no export
surface exists. Deterministic; stdlib."""
from __future__ import annotations

import json
import sys
from typing import Any

from _base import canonical, err, ok

# Deterministic in-memory store keyed by origin domain.
_STORE = {
    "example.com": [
        {"item_id": "it-1", "label": "example login", "fields": {"username": "user@example.com", "password": "REDACTED", "totp_seed": "SEED"}},
    ],
}


class VaultFake:
    def __init__(self) -> None:
        self._state = "kLocked"

    def unlock(self, hint: str) -> dict:
        self._state = "kUnlocked"
        return ok("kUnlocked")

    def lock(self) -> dict:
        self._state = "kLocked"
        return ok(None)

    def list_for_origin(self, origin: dict) -> dict:
        if self._state != "kUnlocked":
            return err("kNotPermitted")
        dom = origin.get("registrable_domain", "")
        items = _STORE.get(dom, [])
        return ok([{"item_id": i["item_id"], "label": i["label"]} for i in items])

    def get_field(self, item_id: str, field_name: str) -> dict:
        if self._state != "kUnlocked":
            return err("kNotPermitted")
        if field_name in {"totp_seed"}:  # seed never leaves the vault.
            return err("kNotPermitted")
        for items in _STORE.values():
            for it in items:
                if it["item_id"] == item_id and field_name in it["fields"]:
                    return ok({"name": field_name, "value": it["fields"][field_name]})
        return err("kUnknownIdentity")

    def totp(self, item_id: str) -> dict:
        if self._state != "kUnlocked":
            return err("kNotPermitted")
        # Deterministic derived code (no clock in the fake): fixed for fixtures.
        return ok("000000")


def call(method: str, args: dict[str, Any]) -> dict:
    fake = VaultFake()
    if args.get("_unlocked", True):
        fake.unlock("test")
    if method == "Unlock":
        return fake.unlock(args.get("hint", ""))
    if method == "Lock":
        return fake.lock()
    if method == "ListForOrigin":
        return fake.list_for_origin(args.get("origin", {}))
    if method == "GetField":
        return fake.get_field(args.get("item_id", ""), args.get("field_name", ""))
    if method == "PutItem":
        return ok([{"item_id": "it-new", "label": "new"}])
    if method == "Totp":
        return fake.totp(args.get("item_id", ""))
    return err("kMalformedInput")


def main(argv: list[str]) -> int:
    payload = json.loads(argv[1] if len(argv) > 1 else sys.stdin.read())
    print(canonical(call(payload.get("method", ""), payload.get("args", {}))))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
